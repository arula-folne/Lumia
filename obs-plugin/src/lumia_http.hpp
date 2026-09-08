#pragma once

#include "lumia_engine.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class LumiaHttpServer {
public:
	LumiaHttpServer();
	~LumiaHttpServer();

	bool start(const std::string &staticRoot, int preferredPort = 0);
	void stop();

	int port() const { return port_; }
	std::string baseUrl() const;
	LumiaEngine &engine() { return engine_; }

	void setFade(bool enabled, double fadeInSec, double fadeOutSec);
	std::string configJson() const;

private:
	LumiaEngine engine_;
	std::unique_ptr<std::thread> thread_;
	std::atomic<bool> running_{false};
	std::atomic<bool> shouldStop_{false};
	int port_ = 0;
	void *serverPtr_ = nullptr; // httplib::Server*

	mutable std::mutex configMutex_;
	bool fadeEnabled_ = true;
	double fadeInSec_ = 0.4;
	double fadeOutSec_ = 0.4;
};
