#include "lumia_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;

namespace {

const char *kAudioExt[] = {".mp3", ".flac", ".m4a", ".aac", ".wav", ".ogg", ".opus"};
const char *kCovers[] = {"cover.png", "cover.jpg", "cover.jpeg", "cover.webp"};

std::string toLower(std::string s)
{
	for (char &c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string &s)
{
	if (s.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	if (n <= 0)
		return {};
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
	return w;
}

std::string wideToUtf8(const wchar_t *w)
{
	if (!w || !*w)
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return {};
	std::string out((size_t)n - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
	return out;
}
#endif

} // namespace

std::string LumiaEngine::makeId(const std::string &path)
{
	static const char *b64 =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	out.reserve((path.size() * 4) / 3 + 4);
	const auto *data = reinterpret_cast<const unsigned char *>(path.data());
	size_t len = path.size();
	for (size_t i = 0; i < len; i += 3) {
		unsigned v = data[i] << 16;
		if (i + 1 < len)
			v |= data[i + 1] << 8;
		if (i + 2 < len)
			v |= data[i + 2];
		out.push_back(b64[(v >> 18) & 63]);
		out.push_back(b64[(v >> 12) & 63]);
		out.push_back(i + 1 < len ? b64[(v >> 6) & 63] : '=');
		out.push_back(i + 2 < len ? b64[v & 63] : '=');
	}
	while (!out.empty() && out.back() == '=')
		out.pop_back();
	return out;
}

bool LumiaEngine::isAudio(const std::string &name)
{
	auto ext = toLower(fs::path(name).extension().string());
	for (auto *e : kAudioExt) {
		if (ext == e)
			return true;
	}
	return false;
}

std::string LumiaEngine::findCover(const std::string &dir)
{
	for (auto *c : kCovers) {
		fs::path p = fs::path(dir) / c;
		if (fs::exists(p) && fs::is_regular_file(p))
			return p.string();
	}
	std::error_code ec;
	for (auto &ent : fs::directory_iterator(dir, ec)) {
		if (!ent.is_regular_file())
			continue;
		auto name = toLower(ent.path().filename().string());
		for (auto *c : kCovers) {
			if (name == c)
				return ent.path().string();
		}
	}
	return {};
}

std::string LumiaEngine::titleFromFile(const std::string &name)
{
	return fs::path(name).stem().string();
}

std::string LumiaEngine::artistFromFile(const std::string &path)
{
#ifdef _WIN32
	std::wstring wpath = utf8ToWide(path);
	if (wpath.empty())
		return {};

	IPropertyStore *store = nullptr;
	HRESULT hr = SHGetPropertyStoreFromParsingName(wpath.c_str(), nullptr, GPS_DEFAULT,
						       IID_PPV_ARGS(&store));
	if (FAILED(hr) || !store)
		return {};

	std::string artist;
	PROPVARIANT var;
	PropVariantInit(&var);
	/* System.Music.Artist ~= Explorer "Contributing artists" */
	hr = store->GetValue(PKEY_Music_Artist, &var);
	if (SUCCEEDED(hr)) {
		PWSTR str = nullptr;
		if (SUCCEEDED(PropVariantToStringAlloc(var, &str)) && str) {
			artist = wideToUtf8(str);
			CoTaskMemFree(str);
		}
	}
	PropVariantClear(&var);
	store->Release();

	/* trim whitespace */
	while (!artist.empty() && (unsigned char)artist.front() <= ' ')
		artist.erase(artist.begin());
	while (!artist.empty() && (unsigned char)artist.back() <= ' ')
		artist.pop_back();
	return artist;
#else
	(void)path;
	return {};
#endif
}

std::string LumiaEngine::jsonEscape(const std::string &s)
{
	std::string o;
	o.reserve(s.size() + 8);
	for (unsigned char c : s) {
		switch (c) {
		case '\\':
			o += "\\\\";
			break;
		case '"':
			o += "\\\"";
			break;
		case '\n':
			o += "\\n";
			break;
		case '\r':
			o += "\\r";
			break;
		case '\t':
			o += "\\t";
			break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				o += buf;
			} else {
				o.push_back((char)c);
			}
		}
	}
	return o;
}

void LumiaEngine::ingestPath(const std::string &path)
{
	std::error_code ec;
	fs::path p = fs::weakly_canonical(fs::path(path), ec);
	if (ec)
		p = fs::path(path);

	if (fs::is_regular_file(p) && isAudio(p.filename().string())) {
		LumiaTrack t;
		t.filePath = p.string();
		t.id = makeId(t.filePath);
		t.title = titleFromFile(p.filename().string());
		t.artist = artistFromFile(t.filePath);
		t.isSingle = true;
		tracks_.push_back(std::move(t));
		return;
	}

	if (!fs::is_directory(p))
		return;

	auto folderName = p.filename().string();
	bool musicFolder = toLower(folderName) == "music";

	std::vector<fs::path> audioFiles;
	std::vector<fs::path> subdirs;
	for (auto &ent : fs::directory_iterator(p, ec)) {
		if (ent.is_regular_file() && isAudio(ent.path().filename().string()))
			audioFiles.push_back(ent.path());
		else if (ent.is_directory())
			subdirs.push_back(ent.path());
	}

	std::sort(audioFiles.begin(), audioFiles.end(), [](const fs::path &a, const fs::path &b) {
		return a.filename().string() < b.filename().string();
	});
	std::sort(subdirs.begin(), subdirs.end(), [](const fs::path &a, const fs::path &b) {
		return a.filename().string() < b.filename().string();
	});

	if (!audioFiles.empty()) {
		std::string cover = musicFolder ? std::string() : findCover(p.string());
		for (auto &fp : audioFiles) {
			LumiaTrack t;
			t.filePath = fp.string();
			t.id = makeId(t.filePath);
			t.title = titleFromFile(fp.filename().string());
			t.artist = artistFromFile(t.filePath);
			if (musicFolder) {
				t.isSingle = true;
			} else {
				t.isSingle = false;
				t.album = folderName;
				t.coverPath = cover;
			}
			tracks_.push_back(std::move(t));
		}
		return;
	}

	/* Parent folder: expand child albums / music */
	for (auto &sub : subdirs)
		ingestPath(sub.string());
}

bool LumiaEngine::setPlaylist(const std::vector<std::string> &paths, std::string &err)
{
	std::lock_guard<std::mutex> lock(mutex_);
	playlistPaths_ = paths;
	tracks_.clear();
	error_.clear();
	root_.clear();

		if (paths.empty()) {
		queue_.clear();
		index_ = -1;
		playing_ = false;
		stopped_ = true;
		err = "Playlist is empty";
		error_ = err;
		return false;
	}

	for (auto &path : paths) {
		if (!path.empty())
			ingestPath(path);
	}

	if (tracks_.empty()) {
		err = "No audio found in playlist";
		error_ = err;
		queue_.clear();
		index_ = -1;
		playing_ = false;
		stopped_ = true;
		return false;
	}

	if (paths.size() == 1)
		root_ = paths[0];
	else
		root_ = "playlist(" + std::to_string(paths.size()) + ")";

	rebuildQueue(false);
	playing_ = false;
	stopped_ = true;
	position_ = 0;
	duration_ = 0;
	seekTo_ = -1;
	bumpMediaUnlocked();
	return true;
}

bool LumiaEngine::setRoot(const std::string &root, std::string &err)
{
	return setPlaylist({root}, err);
}

void LumiaEngine::rebuildQueue(bool keepCurrent)
{
	std::string curId;
	if (keepCurrent && index_ >= 0 && index_ < (int)queue_.size())
		curId = queue_[index_].id;

	queue_ = tracks_;
	if (shuffle_ && queue_.size() > 1) {
		std::mt19937 rng{std::random_device{}()};
		std::shuffle(queue_.begin(), queue_.end(), rng);
	}

	index_ = -1;
	if (queue_.empty())
		return;

	if (!curId.empty()) {
		for (int i = 0; i < (int)queue_.size(); ++i) {
			if (queue_[i].id == curId) {
				index_ = i;
				break;
			}
		}
	}
	if (index_ < 0)
		index_ = 0;
	bumpMediaUnlocked();
}

void LumiaEngine::bumpMediaUnlocked()
{
	mediaGeneration_++;
}

void LumiaEngine::play()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (queue_.empty())
		rebuildQueue(false);
	if (queue_.empty())
		return;
	if (index_ < 0)
		index_ = 0;
	playing_ = true;
	stopped_ = false;
}

void LumiaEngine::pause()
{
	std::lock_guard<std::mutex> lock(mutex_);
	playing_ = false;
}

void LumiaEngine::toggle()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (playing_) {
		playing_ = false;
	} else {
		if (queue_.empty())
			rebuildQueue(false);
		if (!queue_.empty()) {
			if (index_ < 0)
				index_ = 0;
			playing_ = true;
			stopped_ = false;
		}
	}
}

void LumiaEngine::stop()
{
	std::lock_guard<std::mutex> lock(mutex_);
	playing_ = false;
	stopped_ = true;
	position_ = 0;
	seekTo_ = -1;
}

void LumiaEngine::restart()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (queue_.empty())
		rebuildQueue(false);
	if (queue_.empty())
		return;
	if (index_ < 0)
		index_ = 0;
	position_ = 0;
	seekTo_ = -1;
	playing_ = true;
	stopped_ = false;
	bumpMediaUnlocked();
}

void LumiaEngine::advanceAfterEndUnlocked()
{
	if (queue_.empty()) {
		playing_ = false;
		stopped_ = true;
		return;
	}
	if (index_ >= (int)queue_.size() - 1) {
		if (!loop_) {
			playing_ = false;
			stopped_ = true;
			return;
		}
		rebuildQueue(false);
		index_ = queue_.empty() ? -1 : 0;
	} else {
		index_++;
	}
	position_ = 0;
	duration_ = 0;
	seekTo_ = -1;
	playing_ = !queue_.empty();
	stopped_ = !playing_;
	bumpMediaUnlocked();
}

void LumiaEngine::next()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (queue_.empty())
		return;
	bool wasPlaying = playing_ || !stopped_;
	advanceAfterEndUnlocked();
	if (wasPlaying && !queue_.empty()) {
		playing_ = true;
		stopped_ = false;
	}
}

void LumiaEngine::prev()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (queue_.empty())
		return;
	if (position_ > 3.0) {
		position_ = 0;
		seekTo_ = 0;
		playing_ = true;
		stopped_ = false;
		bumpMediaUnlocked();
		return;
	}
	if (index_ <= 0)
		index_ = (int)queue_.size() - 1;
	else
		index_--;
	position_ = 0;
	duration_ = 0;
	seekTo_ = -1;
	playing_ = true;
	stopped_ = false;
	bumpMediaUnlocked();
}

void LumiaEngine::setShuffle(bool on)
{
	std::lock_guard<std::mutex> lock(mutex_);
	shuffle_ = on;
	rebuildQueue(true);
}

void LumiaEngine::setLoop(bool on)
{
	std::lock_guard<std::mutex> lock(mutex_);
	loop_ = on;
}

void LumiaEngine::setProgress(double position, double duration, bool ended)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (position >= 0)
		position_ = position;
	if (duration >= 0)
		duration_ = duration;
	if (ended)
		advanceAfterEndUnlocked();
}

void LumiaEngine::setSeek(double seconds)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (seconds < 0)
		seconds = 0;
	position_ = seconds;
	seekTo_ = seconds;
	stopped_ = false;
}

void LumiaEngine::clearSeek()
{
	std::lock_guard<std::mutex> lock(mutex_);
	seekTo_ = -1;
}

void LumiaEngine::setTransportTimes(double position, double duration)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (position >= 0)
		position_ = position;
	if (duration >= 0)
		duration_ = duration;
}

bool LumiaEngine::getCurrentFile(std::string &path) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (index_ < 0 || index_ >= (int)queue_.size())
		return false;
	path = queue_[index_].filePath;
	return !path.empty();
}

uint64_t LumiaEngine::mediaGeneration() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return mediaGeneration_;
}

bool LumiaEngine::resolveMedia(const std::string &id, std::string &filePath,
			       std::string &coverPath) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &t : tracks_) {
		if (t.id == id) {
			filePath = t.filePath;
			coverPath = t.coverPath;
			return true;
		}
	}
	return false;
}

LumiaState LumiaEngine::snapshot() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	LumiaState s;
	s.playing = playing_;
	s.shuffle = shuffle_;
	s.loop = loop_;
	s.stopped = stopped_;
	s.position = position_;
	s.duration = duration_;
	s.seekTo = seekTo_;
	s.index = index_;
	s.queueLength = (int)queue_.size();
	s.root = root_;
	s.error = error_;
	if (index_ >= 0 && index_ < (int)queue_.size()) {
		s.track = queue_[index_];
		s.hasTrack = true;
	}
	return s;
}

std::string LumiaEngine::snapshotJson() const
{
	auto s = snapshot();
	std::ostringstream o;
	o << std::boolalpha;
	o << "{\"playing\":" << s.playing << ",\"shuffle\":" << s.shuffle << ",\"loop\":" << s.loop
	  << ",\"stopped\":" << s.stopped << ",\"position\":" << s.position
	  << ",\"duration\":" << s.duration << ",\"seekTo\":" << s.seekTo << ",\"index\":" << s.index
	  << ",\"queueLength\":" << s.queueLength << ",\"track\":";
	if (!s.hasTrack) {
		o << "null";
	} else {
		o << "{\"id\":\"" << jsonEscape(s.track.id) << "\",\"title\":\""
		  << jsonEscape(s.track.title) << "\",\"album\":"
		  << (s.track.album.empty() ? "null"
					    : ("\"" + jsonEscape(s.track.album) + "\""))
		  << ",\"artist\":"
		  << (s.track.artist.empty() ? "null"
					     : ("\"" + jsonEscape(s.track.artist) + "\""))
		  << ",\"kind\":\"" << (s.track.isSingle ? "single" : "album")
		  << "\",\"audioUrl\":\"/media/" << jsonEscape(s.track.id) << "\",\"coverUrl\":";
		if (s.track.coverPath.empty())
			o << "null";
		else
			o << "\"/cover/" << jsonEscape(s.track.id) << "\"";
		o << "}";
	}
	o << "}";
	return o.str();
}
