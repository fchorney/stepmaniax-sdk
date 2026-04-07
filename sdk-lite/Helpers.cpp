#include "Helpers.h"
#include <cstdio>
#include <cstring>
#include <random>

using namespace std;

namespace {
    function<void(const string &log)> g_LogCallback = [](const string &log) {
        printf("%6.3f: %s\n", SMX::GetMonotonicTime(), log.c_str());
    };
}

void SMX::Log(const string &s)
{
    if(g_LogCallback)
        g_LogCallback(s);
}

void SMX::SetLogCallback(function<void(const string &log)> callback)
{
    g_LogCallback = callback;
}

string SMX::ssprintf(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int iChars = vsnprintf(nullptr, 0, fmt, va);
    va_end(va);

    if(iChars < 0)
        return string("Error formatting string: ") + fmt;

    string sStr(iChars, '\0');
    va_start(va, fmt);
    vsnprintf(&sStr[0], iChars + 1, fmt, va);
    va_end(va);

    return sStr;
}

string SMX::BinaryToHex(const void *pData_, int iNumBytes)
{
    const unsigned char *pData = (const unsigned char *)pData_;
    string s;
    for(int i = 0; i < iNumBytes; i++)
        s += ssprintf("%02x", pData[i]);
    return s;
}

string SMX::BinaryToHex(const string &sString)
{
    return BinaryToHex(sString.data(), sString.size());
}

double SMX::GetMonotonicTime()
{
    static auto start = chrono::steady_clock::now();
    auto now = chrono::steady_clock::now();
    return chrono::duration<double>(now - start).count();
}

void SMX::GenerateRandom(void *pOut, int iSize)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dist(0, 255);
    uint8_t *p = (uint8_t *)pOut;
    for(int i = 0; i < iSize; i++)
        p[i] = (uint8_t)dist(gen);
}
