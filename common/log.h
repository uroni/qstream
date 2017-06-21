#define LOG_LEVEL 3

#include <string>

void log(std::string str);

#define LL_DEBUG 4
#define LL_INFO 3
#define LL_WARNING 2
#define LL_ERROR 1
#define LOG(x,y) if(y<=LOG_LEVEL) log(x);