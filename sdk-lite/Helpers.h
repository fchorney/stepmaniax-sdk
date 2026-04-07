#ifndef HELPERS_H
#define HELPERS_H

#include <string>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <cstdarg>

namespace SMX
{

void Log(const std::string &s);
void SetLogCallback(std::function<void(const std::string &log)> callback);

std::string ssprintf(const char *fmt, ...);
std::string BinaryToHex(const void *pData, int iNumBytes);
std::string BinaryToHex(const std::string &sString);
double GetMonotonicTime();
void GenerateRandom(void *pOut, int iSize);

}

#endif
