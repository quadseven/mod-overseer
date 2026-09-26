#ifndef MOD_OVERSEER_CRASH_H
#define MOD_OVERSEER_CRASH_H

// A CRASH THAT CAN BE GIVEN A CAUSE (#741).
//
// The dev worldserver segfaulted three times in one day with no backtrace:
// stdout is buffered, so the last lines are lost, and the image has no gdb.
// The binary keeps its debug info, so the return addresses and the load base
// written here are enough to name the source lines offline with
// tools/crash-trace/symbolize.sh against the same image digest.
//
// EVERYTHING IN THE HANDLER IS ASYNC-SIGNAL-SAFE: write(), backtrace() (preloaded
// at install, so the handler never takes the loader's lock) and
// backtrace_symbols_fd(), plus a hand-written hex formatter. The file is opened
// and the load base is read at install, never in the handler.

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <stdint.h>
#include <initializer_list>
#include <string.h>

namespace OverseerCrashTrace
{
inline int TraceFd = STDERR_FILENO;
inline uintptr_t LoadBase = 0;

inline char* Hex(char* out, uintptr_t value)
{
    static char const digits[] = "0123456789abcdef";
    *out++ = '0';
    *out++ = 'x';
    bool started = false;
    for (int shift = (sizeof(value) * 8) - 4; shift >= 0; shift -= 4)
    {
        unsigned digit = (value >> shift) & 0xf;
        if (digit || started || shift == 0)
        {
            *out++ = digits[digit];
            started = true;
        }
    }
    return out;
}

inline void WriteBoth(char const* data, size_t size)
{
    (void)write(TraceFd, data, size);
    if (TraceFd != STDERR_FILENO)
        (void)write(STDERR_FILENO, data, size);
}

inline void Handler(int signalNumber, siginfo_t* info, void*)
{
    char line[128];
    char* p = line;
    char const* name = signalNumber == SIGSEGV ? "SIGSEGV" :
        signalNumber == SIGBUS ? "SIGBUS" :
        signalNumber == SIGFPE ? "SIGFPE" : "SIGABRT";
    while (*name)
        *p++ = *name++;
    char const* addr = " addr ";
    while (*addr)
        *p++ = *addr++;
    p = Hex(p, reinterpret_cast<uintptr_t>(info ? info->si_addr : nullptr));
    char const* when = " time ";
    while (*when)
        *p++ = *when++;
    p = Hex(p, static_cast<uintptr_t>(time(nullptr)));
    *p++ = '\n';
    WriteBoth(line, static_cast<size_t>(p - line));

    p = line;
    char const* base = "base ";
    while (*base)
        *p++ = *base++;
    p = Hex(p, LoadBase);
#if !defined(__linux__)
    char const* fallback = " (no /proc/self/maps)";
    while (*fallback)
        *p++ = *fallback++;
#endif
    *p++ = '\n';
    WriteBoth(line, static_cast<size_t>(p - line));

    void* frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, TraceFd);
    if (TraceFd != STDERR_FILENO)
        backtrace_symbols_fd(frames, count, STDERR_FILENO);

    // THE CRASH STAYS A CRASH. SA_RESETHAND has put the default action back,
    // so this ends the process exactly as an untraced crash would: exit 139
    // for a segfault, and the same restart the pod always had.
    raise(signalNumber);
}

inline uintptr_t FindLoadBase()
{
#if defined(__linux__)
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0)
        return 0;
    executable[length] = '\0';
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0)
        return 0;
    char maps[16384];
    ssize_t size = read(fd, maps, sizeof(maps) - 1);
    close(fd);
    if (size <= 0)
        return 0;
    maps[size] = '\0';
    for (char* row = maps; row < maps + size;)
    {
        char* end = static_cast<char*>(memchr(row, '\n', static_cast<size_t>(maps + size - row)));
        if (!end)
            end = maps + size;
        if (strstr(row, executable))
        {
            uintptr_t base = 0;
            for (char* c = row; c < end && *c != '-'; ++c)
                base = (base << 4) + (*c >= '0' && *c <= '9' ? *c - '0' :
                    *c >= 'a' && *c <= 'f' ? *c - 'a' + 10 : *c - 'A' + 10);
            return base;
        }
        row = end + (end < maps + size);
    }
#endif
    return 0;
}

inline void InstallCrashTrace(char const* logDir)
{
    (void)setvbuf(stderr, nullptr, _IONBF, 0);
    char path[4096];
    char const* dir = logDir && *logDir ? logDir : ".";
    size_t length = strlen(dir);
    if (length + sizeof("/crash-trace.log") <= sizeof(path))
    {
        memcpy(path, dir, length);
        if (length && dir[length - 1] != '/')
            path[length++] = '/';
        memcpy(path + length, "crash-trace.log", sizeof("crash-trace.log"));
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0)
            TraceFd = fd;
    }
    void* preload[1];
    (void)backtrace(preload, 1);
    LoadBase = FindLoadBase();
    struct sigaction action = {};
    action.sa_sigaction = Handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    stack_t stack = {};
    stack.ss_size = SIGSTKSZ * 4;
    stack.ss_sp = malloc(stack.ss_size);
    if (stack.ss_sp && sigaltstack(&stack, nullptr) == 0)
        action.sa_flags |= SA_ONSTACK;
    for (int signalNumber : {SIGSEGV, SIGBUS, SIGFPE, SIGABRT})
        (void)sigaction(signalNumber, &action, nullptr);
}
}

inline void InstallCrashTrace(char const* logDir)
{
    OverseerCrashTrace::InstallCrashTrace(logDir);
}

#endif
