#include "FileUtil.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdexcept>

FileUtil::FileUtil(const std::string& file_name)
{
	fd = ::open(file_name.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
	if (fd < 0) throw std::runtime_error("open() failed");
}

FileUtil::~FileUtil()
{
	if (fd >= 0) ::close(fd);
}

void FileUtil::append(const char* data, size_t len)
{
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
