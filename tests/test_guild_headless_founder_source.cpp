/*
 * A guild's founder counts as online when a client holds him or when he is a
 * headless roster bot in the world (the adapter compiles only in CI, so this
 * reads the source as text).
 *
 * A family played with no client has a lead with no socket; asking for a
 * client alone meant such a family could never found its guild.
 */

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}
}  // namespace

int main()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::string const source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Check("source is readable", !source.empty());

    auto const at = source.find("facts.founderOnline =");
    Check("the founder fact is set", at != std::string::npos);
    std::string const statement = source.substr(at, 400);
    Check("a client still counts", statement.find("ClientAttached(founder)") != std::string::npos);
    Check("a headless roster bot in the world counts",
          statement.find("founder->IsInWorld()") != std::string::npos &&
              statement.find("HeadlessRosterCache()") != std::string::npos);
    Check("the plain ClientAttached-only form is gone",
          source.find("facts.founderOnline = ClientAttached(founder);") == std::string::npos);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
