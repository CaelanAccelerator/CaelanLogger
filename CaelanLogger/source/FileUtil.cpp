#include "FileUtil.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdexcept>
#include <TimeUtil.h>
#include <cstring>
#include <cstdlib>
#include <filesystem>

static std::filesystem::path pick_log_dir(std::filesystem::path requested) {
	// 1) if user set CAELAN_LOG_DIR env variable, use it
    if (const char* env = std::getenv("CAELAN_LOG_DIR"); env && *env) {
        return std::filesystem::path(env);
    }

	// if user enter the requested path, use it directly
    if (!requested.empty() && requested != "./log" && requested != "log") {
        return requested;
    }

    // 3) Linux/WSL： ~/.local/state/caelanlogger/logs
    if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "caelanlogger" / "logs";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "caelanlogger" / "logs";
    }

    // 4) fallback
    return std::filesystem::current_path() / "log";
}

FileUtil::FileUtil(std::string dir, std::string prefix)   
    : dir_(pick_log_dir(std::filesystem::path(dir))), 
      prefix_(std::move(prefix))
{
	//use absolute path inorder to avoid confusion
    dir_ = std::filesystem::absolute(dir_);

	std::error_code ec;// to make sure directory exists
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        throw std::runtime_error("Failed to create log dir: " + dir_.string() + " (" + ec.message() + ")" + std::strerror(errno));
    }
}

FileUtil::~FileUtil()
{
	if (fd >= 0) ::close(fd);
}

// append data to the file
void FileUtil::append(const char* data, size_t len)
{
    if (fd < 0)
    {
        if (!openFile(generateFileName())) {
            throw std::runtime_error(std::string("write() failed: ") + std::strerror(errno));
        }
    }
    if (shouldRoll(len)) 
    {
		roll();
    }
    size_t writtenDown = 0;
    // to prevent if write() doesn't finish.
    while (writtenDown < len) {
        ssize_t n = ::write(fd, data + writtenDown, len - writtenDown);
        if (n > 0) {
            writtenDown += static_cast<size_t>(n);
            writtenBytes += n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue; // retry
        // Other errors: disk full, permission, etc.
        // For now: throw or set an error flag.
        throw std::runtime_error(std::string("write() failed: ") + std::strerror(errno));
    }
}


std::string FileUtil::makeFullPath(const std::string& filename) const
{
    return (dir_ / filename).string();
}

inline bool FileUtil::openFile(const std::string& file_name)
{
    std::error_code ec;// to make sure directory exists
    std::filesystem::create_directories(dir_, ec);
    if (ec) return false;

    const std::string full = makeFullPath(file_name); 
    fd = ::open(full.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
    if (fd < 0) return false;
	return true;
}

void FileUtil::closeFile() {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
	}
}


inline void FileUtil::roll()
{
    ::close(fd);
    if (!openFile(generateFileName())) 
    {
        throw std::runtime_error(std::string("write() failed: ") + std::strerror(errno));
    }
	writtenBytes = 0;
}

inline bool FileUtil::shouldRoll(size_t bufSize)
{
    return writtenBytes + bufSize > FILE_MAX_SIZE;
}

inline std::string FileUtil::generateFileName()
{
    static int order = 0;
    std::string timeStr = LogTime::nowDateString();
    order = (order + 1) % 10000;
    return timeStr + "_LOG_" + std::to_string(order);
}