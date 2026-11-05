#include "overseer_crash.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    char directory[] = "/tmp/overseer-crash-XXXXXX";
    assert(mkdtemp(directory));
    pid_t const child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        InstallCrashTrace(directory);
        volatile int* address = nullptr;
        *address = 1;
        _exit(0);
    }

    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGSEGV);

    std::ifstream trace(std::string(directory) + "/crash-trace.log");
    std::string contents((std::istreambuf_iterator<char>(trace)),
                         std::istreambuf_iterator<char>());
    assert(contents.find("SIGSEGV addr ") != std::string::npos);
    assert(contents.find("base 0x") != std::string::npos);
    size_t const baseLine = contents.find("base 0x");
    size_t const frameStart = contents.find('\n', baseLine);
    assert(frameStart != std::string::npos);
    // A frame line: macOS writes "0   name ...", glibc "name(+0x..)[0x..]",
    // and the glibc on ubuntu-26.04 runners "name(+0x..) [0x..]" (seen in CI).
    bool const frames = contents.find("\n0 ", frameStart) != std::string::npos ||
                        contents.find("[0x", frameStart) != std::string::npos;
    if (!frames)
        fprintf(stderr, "no frame line in the trace:\n%s\n", contents.c_str());
    assert(frames);
    return 0;
}
