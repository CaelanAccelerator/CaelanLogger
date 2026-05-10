#pragma once
#include <atomic>
#include <string>
#include <filesystem>

constexpr size_t FILE_MAX_SIZE = 256ull * 1024 * 1024;

class FileUtil
{
public:
	/**
	 *  @brief open the file we need to write
	 *  @param name of the file
	 */
	explicit FileUtil(std::string dir = "./log", std::string prefix = "caelogger");

	/**
	 *  @brief close the file and clean resources
	 */
	~FileUtil();

	/**
	 *  @brief append data to the file
	 *  @param data pointer to the data to append
	 *  @param len length of the data to append
	 */
	void append(const char *data, size_t len);

	unsigned long getWrittenBytes() const { return writtenBytes; }
	void add_dropped(size_t n = 1);
	void roll();

private:
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
