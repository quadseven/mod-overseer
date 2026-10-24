/* Source contract for patches/mod-playerbots/0014-quest-poi-void-plane-is-not-ground.patch.
 *
 * The copy that ships refuses a void-plane height as an `else if` at both
 * sites in NewRpgBaseAction.cpp, so the unloaded-grid forgiveness keeps its
 * substituted z. This module's copy must add the same lines, or its CI tests
 * different code from what ships. Run from the repository root. */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}
}  // namespace

int main()
{
    std::ifstream in("patches/mod-playerbots/0014-quest-poi-void-plane-is-not-ground.patch");
    if (!in)
        return Fail("cannot read the void-plane patch");
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string const file = "+++ b/src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp";
    std::size_t const at = text.find(file);
    if (at == std::string::npos)
        return Fail("patch no longer touches NewRpgBaseAction.cpp");
    std::string const section = text.substr(at);

    int refusals = 0;
    int elseRefusals = 0;
    std::size_t pos = 0;
    while ((pos = section.find("dz <= MIN_HEIGHT)", pos)) != std::string::npos)
    {
        std::size_t const lineStart = section.rfind('\n', pos) + 1;
        std::string const line = section.substr(lineStart, pos - lineStart);
        if (line.size() > 0 && line[0] == '+' && line.find("//") == std::string::npos)
        {
            ++refusals;
            if (line.find("else if (") != std::string::npos)
                ++elseRefusals;
        }
        ++pos;
    }
    if (refusals != 2)
        return Fail("NewRpgBaseAction.cpp must carry exactly two added void-plane refusals");
    if (elseRefusals != 2)
        return Fail("both void-plane refusals in NewRpgBaseAction.cpp must be `else if`");
    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
