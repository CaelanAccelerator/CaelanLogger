#pragma once
#include <atomic>
#include <cstring>
#include <string>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
#include <system_error>
#include "TimeUtil.h"

constexpr size_t FILE_MAX_SIZE = 256ull * 1024 * 1024;

template <typename Derived>
class FileUtil
{
public:
	explicit FileUtil(std::string dir = "./log", std::string prefix = "caelogger");
	~FileUtil();

	unsigned long getWrittenBytes() const { return writtenBytes; }
	void add_dropped(size_t n = 1);
	void roll();

protected:
	std::filesystem::path pick_log_dir(std::filesystem::path);
	std::atomic<size_t> dropped_{0};
	std::filesystem::path dir_;
	std::string prefix_;

	int fd_{-1};
	unsigned long writtenBytes{0};
	bool shouldRoll(size_t bufSize);
	bool openFile(const std::string &filename);
	void closeFile();
	std::string makeFullPath(const std::string &filename) const;
	std::string generateFileName();
};

template <typename Derived>
inline std::filesystem::path FileUtil<Derived>::pick_log_dir(std::filesystem::path requested)
{
	if (const char *env = std::getenv("CAELAN_LOG_DIR"); env && *env)
	{
		return std::filesystem::path(env);
	}

	if (!requested.empty() && requested != "./log" && requested != "log")
	{
		return requested;
	}

	return std::filesystem::current_path() / "log";
}

template <typename Derived>
FileUtil<Derived>::FileUtil(std::string dir, std::string prefix)
		: dir_(pick_log_dir(std::filesystem::path(dir))), prefix_(std::move(prefix))
{
	dir_ = std::filesystem::absolute(dir_);
	std::error_code ec;
	std::filesystem::create_directories(dir_, ec);
	if (ec)
	{
		throw std::runtime_error("Failed to create log dir: " + dir_.string() + " (" + ec.message() + ")" + std::strerror(errno));
	}
}

template <typename Derived>
FileUtil<Derived>::~FileUtil()
{
	if (fd_ >= 0)
		::close(fd_);
}

template <typename Derived>
void FileUtil<Derived>::add_dropped(size_t n)
{
	dropped_.fetch_add(n, std::memory_order_relaxed);
}

template <typename Derived>
void FileUtil<Derived>::roll()
{
	size_t n = dropped_.exchange(0, std::memory_order_relaxed);
	if (n > 0 && fd_ >= 0)
	{
		constexpr size_t kDropMsgCap = 64;
		char *msg = new char[kDropMsgCap];
		int len = std::snprintf(msg, kDropMsgCap, "dropped: %zu\n", n);
		if (len > 0)
		{
			static_cast<Derived *>(this)->writeDropMessage(msg, len);
		}
		else
		{
			delete[] msg;
		}
	}

	if (!openFile(generateFileName()))
	{
		throw std::runtime_error(std::string("write() failed: ") + std::strerror(errno));
	}
	writtenBytes = 0;
}

template <typename Derived>
bool FileUtil<Derived>::shouldRoll(size_t bufSize)
{
	return writtenBytes + bufSize > FILE_MAX_SIZE;
}

template <typename Derived>
bool FileUtil<Derived>::openFile(const std::string &file_name)
{
	std::error_code ec;
	std::filesystem::create_directories(dir_, ec);
	if (ec)
	{
		return false;
	}

	const std::string full = makeFullPath(file_name);
	fd_ = ::open(full.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
	return fd_ >= 0;
}

template <typename Derived>
void FileUtil<Derived>::closeFile()
{
	if (fd_ >= 0)
	{
		close(fd_);
		fd_ = -1;
	}
}

template <typename Derived>
std::string FileUtil<Derived>::makeFullPath(const std::string &filename) const
{
	return (dir_ / filename).string();
}

template <typename Derived>
std::string FileUtil<Derived>::generateFileName()
{
	static int order = 0;
	std::string timeStr = LogTime::nowDateString();
	order = (order + 1) % 10000;
	return timeStr + "_LOG_" + std::to_string(order);
}
