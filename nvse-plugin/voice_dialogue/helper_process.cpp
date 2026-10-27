#include "helper_process.h"

bool HelperProcess::Start(const std::string &exePath, const std::string &args, const std::string &workDir)
{
	SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
	HANDLE stdinRead = nullptr, stdoutWrite = nullptr;
	if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0) || !CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0))
		return false;
	// Our ends must not be inherited, or the helper never sees EOF when we close them.
	SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = stdinRead;
	si.hStdOutput = stdoutWrite;
	si.hStdError = stdoutWrite;

	std::string cmdLine = "\"" + exePath + "\" " + args;
	PROCESS_INFORMATION pi{};
	BOOL ok = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workDir.c_str(), &si, &pi);
	CloseHandle(stdinRead);
	CloseHandle(stdoutWrite);
	if (!ok)
		return false;

	// Kill the helper automatically if the game exits or crashes.
	job = CreateJobObjectA(nullptr, nullptr);
	if (job)
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
		info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
		AssignProcessToJobObject(job, pi.hProcess);
	}
	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	process = pi.hProcess;

	reader = std::thread(&HelperProcess::ReadLoop, this);
	return true;
}

void HelperProcess::ReadLoop()
{
	std::string pending;
	char buf[4096];
	DWORD got = 0;
	while (ReadFile(stdoutRead, buf, sizeof(buf), &got, nullptr) && got)
	{
		pending.append(buf, got);
		size_t nl;
		while ((nl = pending.find('\n')) != std::string::npos)
		{
			std::string line = pending.substr(0, nl);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			pending.erase(0, nl + 1);
			std::lock_guard<std::mutex> guard(lock);
			lines.push_back(std::move(line));
		}
	}
}

void HelperProcess::Send(const std::string &line)
{
	if (!stdinWrite)
		return;
	std::string out = line + "\n";
	DWORD written = 0;
	WriteFile(stdinWrite, out.data(), (DWORD)out.size(), &written, nullptr);
}

bool HelperProcess::Poll(std::string &line)
{
	std::lock_guard<std::mutex> guard(lock);
	if (lines.empty())
		return false;
	line = std::move(lines.front());
	lines.pop_front();
	return true;
}

void HelperProcess::Stop()
{
	if (process)
	{
		Send("{\"cmd\":\"quit\"}");
		if (WaitForSingleObject(process, 1000) == WAIT_TIMEOUT)
			TerminateProcess(process, 0);
	}
	if (stdinWrite) CloseHandle(stdinWrite), stdinWrite = nullptr;
	// Closing our read end unblocks ReadFile once the helper is gone.
	if (reader.joinable()) reader.join();
	if (stdoutRead) CloseHandle(stdoutRead), stdoutRead = nullptr;
	if (process) CloseHandle(process), process = nullptr;
	if (job) CloseHandle(job), job = nullptr;
}
