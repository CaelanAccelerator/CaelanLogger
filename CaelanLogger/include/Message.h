#include<string>
#include"Level.h"
class Message
{
public:
	Message();
	~Message();

private:
	Level level;
	std::string content;
};

Message::Message()
{
}

Message::~Message()
{
}