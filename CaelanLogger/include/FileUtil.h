#pragma once
#include <string>

constexpr size_t FILE_MAX_SIZE = 256ull * 1024 * 1024;

class FileUtil
{
public:
	/**
	*  @brief open the file we need to write
	*  @param name of the file
	*/
	explicit FileUtil();

	/**
	*  @brief close the file and clean resources
	*/
	~FileUtil();

	/**
	*  @brief append data to the file
	*  @param data pointer to the data to append
	*  @param len length of the data to append
	*/
	void append(const char* data, size_t len);

	unsigned long getWrittenBytes() const { return writtenBytes; }
private:
	int fd{ -1 };
	unsigned long writtenBytes{ 0 };
	bool shouldRoll(size_t bufSize);
	void roll();
	bool openFile(const std::string& filename);
	void closeFile();
	std::string generateFileName();
};
