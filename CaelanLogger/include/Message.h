#include<string>
#include"Level.h"
class Message
{
public:
	Message();
	~Message();

private:
	CaelanLogger::Level level;
	std::string content;
};

Message::Message()
{
}

Message::~Message()
{
}