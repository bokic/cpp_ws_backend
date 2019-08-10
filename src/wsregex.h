#pragma once

#include <regex>

namespace backend {

class wsregex: public std::regex
{
public:
    wsregex(const char *text);
};

};
