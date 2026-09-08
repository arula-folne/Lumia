#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct LumiaTrack {
	std::string id;
	std::string title;
	std::string album;
	std::string artist;
	std::string filePath;
	std::string coverPath;
	bool isSingle = true;
};

struct LumiaState {
	bool playing = false;
	bool shuffle = true;
	bool loop = true;
	bool stopped = true;
	double position = 0;
	double duration = 0;
	double seekTo = -1; // seconds; <0 means none
	int index = -1;
	int queueLength = 0;
	std::string root;
	std::string error;
	LumiaTrack track;
	bool hasTrack = false;
};

class LumiaEngine {
public:
	/* Returns false on error. Unchanged playlist paths are a no-op success. */
	bool setPlaylist(const std::vector<std::string> &paths, std::string &err);
	bool setRoot(const std::string &root, std::string &err);
	bool playlistPathsEqual(const std::vector<std::string> &paths) const;

	void play();
	void pause();
	void toggle();
	void stop();
	void restart();
	void next();
	void prev();
	void setShuffle(bool on);
	void setLoop(bool on);
	void setProgress(double position, double duration, bool ended);
	void setSeek(double seconds);
	void clearSeek();
	void setTransportTimes(double position, double duration);

	bool getCurrentFile(std::string &path) const;
	uint64_t mediaGeneration() const;

	LumiaState snapshot() const;
	std::string snapshotJson() const;

	bool resolveMedia(const std::string &id, std::string &filePath, std::string &coverPath) const;

private:
	void rebuildQueue(bool keepCurrent);
	void ingestPath(const std::string &path);
	void advanceAfterEndUnlocked();
	void bumpMediaUnlocked();

	static std::string makeId(const std::string &path);
	static bool isAudio(const std::string &name);
	static std::string findCover(const std::string &dir);
	static std::string titleFromFile(const std::string &name);
	static std::string artistFromFile(const std::string &path);
	static std::string jsonEscape(const std::string &s);

	mutable std::mutex mutex_;
	std::vector<std::string> playlistPaths_;
	std::string root_;
	std::vector<LumiaTrack> tracks_;
	std::vector<LumiaTrack> queue_;
	int index_ = -1;
	bool playing_ = false;
	bool shuffle_ = true;
	bool loop_ = true;
	bool stopped_ = true;
	double position_ = 0;
	double duration_ = 0;
	double seekTo_ = -1;
	uint64_t mediaGeneration_ = 0;
	std::string error_;
};
