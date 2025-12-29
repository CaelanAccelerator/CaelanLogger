#pragma once
#include <string>


class FileUtil
{
public:
	/**
	*  @brief open the file we need to write
	*  @param name of the file
	*/
	explicit FileUtil(const std::string& file_name);

	/**
	*  @brief close the file and clean resources
	*/
	~FileUtil();

	void append(const char* data, size_t len);

	unsigned long getWrittenBytes() const { return writtenBytes; }
private:
	int fd{ -1 };
	unsigned long writtenBytes{ 0 };
};
