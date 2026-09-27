#include "mod_log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace {

CRITICAL_SECTION g_logLock;
INIT_ONCE g_logLockOnce = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK InitLogLock(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_logLock);
    return TRUE;
}

void GetLogPath(char* outPath, size_t outSize) {
    char modulePath[MAX_PATH] = {};
    HMODULE thisModule = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetLogPath), &thisModule);
    GetModuleFileNameA(thisModule, modulePath, MAX_PATH);

    const char* lastSlash = strrchr(modulePath, '\\');
    size_t dirLen = lastSlash ? static_cast<size_t>(lastSlash - modulePath + 1) : 0;
    if (dirLen >= outSize) dirLen = 0;
    memcpy(outPath, modulePath, dirLen);
    strcpy_s(outPath + dirLen, outSize - dirLen, "th06nc_native_coop.log");
}

} // namespace

void ModLog(const char* fmt, ...) {
    InitOnceExecuteOnce(&g_logLockOnce, InitLogLock, nullptr, nullptr);
    EnterCriticalSection(&g_logLock);

    char logPath[MAX_PATH] = {};
    GetLogPath(logPath, MAX_PATH);

    FILE* f = nullptr;
    if (fopen_s(&f, logPath, "a") == 0 && f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }

    LeaveCriticalSection(&g_logLock);
}
