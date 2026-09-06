#include "lumia_http.hpp"

#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "httplib.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string mimeFor(const std::string &path)
{
	auto ext = fs::path(path).extension().string();
	for (char &c : ext)
		c = (char)tolower((unsigned char)c);
	if (ext == ".html")
		return "text/html; charset=utf-8";
	if (ext == ".js")
		return "application/javascript; charset=utf-8";
	if (ext == ".css")
		return "text/css; charset=utf-8";
	if (ext == ".png")
		return "image/png";
	if (ext == ".jpg" || ext == ".jpeg")
		return "image/jpeg";
	if (ext == ".webp")
		return "image/webp";
	if (ext == ".mp3")
		return "audio/mpeg";
	if (ext == ".flac")
		return "audio/flac";
	if (ext == ".m4a")
		return "audio/mp4";
	if (ext == ".aac")
		return "audio/aac";
	if (ext == ".wav")
		return "audio/wav";
	if (ext == ".ogg" || ext == ".opus")
		return "audio/ogg";
	return "application/octet-stream";
}

LumiaHttpServer::LumiaHttpServer() = default;

LumiaHttpServer::~LumiaHttpServer()
{
	stop();
}

std::string LumiaHttpServer::baseUrl() const
{
	return "http://127.0.0.1:" + std::to_string(port_);
}

bool LumiaHttpServer::start(const std::string &staticRoot, int preferredPort)
{
	stop();
	shouldStop_ = false;

	auto *svr = new httplib::Server();
	serverPtr_ = svr;

	fs::path root = fs::path(staticRoot);
	svr->set_mount_point("/overlay", root.string());

	svr->Get("/overlay/", [root](const httplib::Request &, httplib::Response &res) {
		std::ifstream ifs((root / "index.html").string(), std::ios::binary);
		if (!ifs) {
			res.status = 404;
			res.set_content("overlay missing", "text/plain");
			return;
		}
		std::ostringstream ss;
		ss << ifs.rdbuf();
		res.set_content(ss.str(), "text/html; charset=utf-8");
	});

	svr->Get("/", [](const httplib::Request &, httplib::Response &res) {
		res.set_redirect("/overlay/");
	});

	svr->Get("/api/state", [this](const httplib::Request &, httplib::Response &res) {
		res.set_content(engine_.snapshotJson(), "application/json; charset=utf-8");
	});

	svr->Post("/api/library", [this](const httplib::Request &req, httplib::Response &res) {
		// {"root":"..."}
		auto body = req.body;
		auto pos = body.find("\"root\"");
		std::string folder;
		if (pos != std::string::npos) {
			auto q1 = body.find('"', pos + 6);
			q1 = body.find('"', q1 + 1);
			auto q2 = body.find('"', q1 + 1);
			if (q1 != std::string::npos && q2 != std::string::npos)
				folder = body.substr(q1 + 1, q2 - q1 - 1);
			std::string un;
			for (size_t i = 0; i < folder.size(); ++i) {
				if (folder[i] == '\\' && i + 1 < folder.size()) {
					char n = folder[++i];
					if (n == '\\' || n == '"' || n == '/')
						un.push_back(n == '/' ? '/' : n);
					else
						un.push_back(n);
				} else {
					un.push_back(folder[i]);
				}
			}
			folder = un;
		}
		std::string err;
		if (folder.empty() || !engine_.setRoot(folder, err)) {
			res.status = 400;
			res.set_content(std::string("{\"error\":\"") + (err.empty() ? "bad root" : err) +
						"\"}",
					"application/json");
			return;
		}
		engine_.play();
		res.set_content(engine_.snapshotJson(), "application/json; charset=utf-8");
	});

	svr->Post("/api/control", [this](const httplib::Request &req, httplib::Response &res) {
		auto &b = req.body;
		auto has = [&](const char *s) { return b.find(s) != std::string::npos; };
		if (has("\"clearSeek\"") || has("\"seekApplied\"")) {
			engine_.clearSeek();
		} else if (has("\"seek\"")) {
			auto p = b.find("\"seconds\"");
			if (p == std::string::npos)
				p = b.find("\"position\"");
			double sec = 0;
			if (p != std::string::npos) {
				p = b.find(':', p);
				if (p != std::string::npos)
					sec = atof(b.c_str() + p + 1);
			}
			engine_.setSeek(sec);
		} else if (has("\"play\""))
			engine_.play();
		else if (has("\"pause\""))
			engine_.pause();
		else if (has("\"stop\""))
			engine_.stop();
		else if (has("\"restart\""))
			engine_.restart();
		else if (has("\"toggle\""))
			engine_.toggle();
		else if (has("\"next\""))
			engine_.next();
		else if (has("\"prev\""))
			engine_.prev();
		else if (has("\"progress\"")) {
			double pos = 0, dur = 0;
			bool ended = has("\"ended\":true") || has("\"ended\": true");
			auto extract = [&](const char *key) -> double {
				auto p = b.find(key);
				if (p == std::string::npos)
					return -1;
				p = b.find(':', p);
				if (p == std::string::npos)
					return -1;
				return atof(b.c_str() + p + 1);
			};
			pos = extract("\"position\"");
			dur = extract("\"duration\"");
			engine_.setProgress(pos, dur, ended);
		} else if (has("\"shuffle\"")) {
			bool on = true;
			if (has("\"enabled\":false") || has("\"enabled\": false"))
				on = false;
			engine_.setShuffle(on);
		}
		res.set_content(engine_.snapshotJson(), "application/json; charset=utf-8");
	});

	auto sendFile = [](httplib::Response &res, const std::string &path) {
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs) {
			res.status = 404;
			res.set_content("Not found", "text/plain");
			return;
		}
		std::ostringstream ss;
		ss << ifs.rdbuf();
		res.set_content(ss.str(), mimeFor(path));
	};

	svr->Get(R"(/media/([^/]+))", [this, sendFile](const httplib::Request &req,
							httplib::Response &res) {
		std::string id = req.matches[1];
		std::string file, cover;
		if (!engine_.resolveMedia(id, file, cover)) {
			res.status = 404;
			res.set_content("Not found", "text/plain");
			return;
		}
		sendFile(res, file);
	});

	svr->Get(R"(/cover/([^/]+))", [this, sendFile](const httplib::Request &req,
							httplib::Response &res) {
		std::string id = req.matches[1];
		std::string file, cover;
		if (!engine_.resolveMedia(id, file, cover) || cover.empty()) {
			res.status = 404;
			res.set_content("Not found", "text/plain");
			return;
		}
		sendFile(res, cover);
	});

	int port = preferredPort > 0 ? preferredPort : 18787;
	bool bound = false;
	for (int i = 0; i < 30; ++i) {
		int tryPort = port + i;
		if (svr->bind_to_port("127.0.0.1", tryPort)) {
			port_ = tryPort;
			bound = true;
			break;
		}
	}
	if (!bound) {
		delete svr;
		serverPtr_ = nullptr;
		return false;
	}

	running_ = true;
	thread_ = std::make_unique<std::thread>([this, svr]() {
		svr->listen_after_bind();
		running_ = false;
	});
	return true;
}

void LumiaHttpServer::stop()
{
	shouldStop_ = true;
	if (serverPtr_) {
		auto *svr = reinterpret_cast<httplib::Server *>(serverPtr_);
		svr->stop();
	}
	if (thread_ && thread_->joinable())
		thread_->join();
	thread_.reset();
	if (serverPtr_) {
		delete reinterpret_cast<httplib::Server *>(serverPtr_);
		serverPtr_ = nullptr;
	}
	running_ = false;
	port_ = 0;
}
