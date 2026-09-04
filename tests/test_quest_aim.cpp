#include "overseer_decisions.h"

#include <cstdio>
#include <map>
#include <string>

using OverseerDecisions::QuestAimsAfterRead;

namespace
{

int failures = 0;

void Expect(bool condition, char const* what)
{
    if (!condition)
    {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

void TestFailedReadRetainsAim()
{
    std::map<std::string, uint32_t> previous{{"Grug", 90}};
    std::map<std::string, uint32_t> empty;
    Expect(QuestAimsAfterRead(previous, empty, false) == previous,
           "failed read retains the last successful aim");
}

void TestSuccessfulEmptyReadClearsAim()
{
    std::map<std::string, uint32_t> previous{{"Grug", 90}};
    std::map<std::string, uint32_t> empty;
    Expect(QuestAimsAfterRead(previous, empty, true).empty(),
           "successful empty read means the aim was deliberately cleared");
}

void TestSuccessfulReadReplacesAim()
{
    std::map<std::string, uint32_t> previous{{"Grug", 90}};
    std::map<std::string, uint32_t> loaded{{"Grug", 233}, {"Og", 90}};
    Expect(QuestAimsAfterRead(previous, loaded, true) == loaded,
           "successful read replaces the previous aim set");
}

} // namespace

int main()
{
    TestFailedReadRetainsAim();
    TestSuccessfulEmptyReadClearsAim();
    TestSuccessfulReadReplacesAim();
    return failures;
}
