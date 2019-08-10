#include "wsregex.h"


backend::wsregex::wsregex(const char *text)
    : std::regex(text)
{
}
