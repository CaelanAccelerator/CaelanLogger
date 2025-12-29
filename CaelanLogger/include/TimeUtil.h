#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace LogTime {
	std::string nowString();
	std::string nowTimeOnlyString();
	std::string nowDateString();
}