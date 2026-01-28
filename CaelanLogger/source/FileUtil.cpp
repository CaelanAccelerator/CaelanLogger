#include "FileUtil.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdexcept>
#include <TimeUtil.h>

FileUtil::FileUtil(std::string dir, std::string prefix)   
    : dir_(std::move(dir)), prefix_(std::move(prefix))    
{
	std::error_code ec;// to make sure directory exists
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        throw std::runtime_error("Failed to create log dir: " + dir_.string() + " (" + ec.message() + ")");
    }
}

FileUtil::~FileUtil()
{
	if (fd >= 0) ::close(fd);
}

void FileUtil::append(const char* data, size_t len)
{
    if (fd < 0)
    {
        if (!openFile(generateFileName())) {
            throw std::runtime_error("Failed to open file");
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
        throw std::runtime_error("write() failed");
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
        throw std::runtime_error("Failed to roll log file");
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