#pragma once

// Runs the speech helper (vdm_helper.exe) as a hidden child process and talks
// to it one JSON line at a time over its stdin/stdout.

#include <Windows.h>

#include <deque>
#include <mutex>
#include <string>
#include <thread>

class HelperProcess
{
public:
	~HelperProcess() { Stop(); }

	bool Start(const std::string &exePath, const std::string &args, const std::string &workDir);
	void Send(const std::string &line);
	bool Poll(std::string &line);	// next line from the helper, if any
	void Stop();
	bool IsRunning() const { return process != nullptr; }

private:
	void ReadLoop();

	HANDLE process = nullptr;
	HANDLE job = nullptr;
	HANDLE stdinWrite = nullptr;
	HANDLE stdoutRead = nullptr;
	std::thread reader;
	std::mutex lock;
	std::deque<std::string> lines;
};
