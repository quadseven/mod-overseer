/*
 * Source contract: the adapter asks FamilyOwnsInstance before it adopts a run
 * found at IDLE, and again before a running phase keeps a run whose only
 * occupant is a minority of the family with no run row.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

void Require(std::string const& text, char const* needle, char const* what)
{
    if (text.find(needle) != std::string::npos)
        return;
    std::cerr << "FAIL " << what << ": missing '" << needle << "'\n";
    ++failures;
}

std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const start = text.find(from);
    std::size_t const end = start == std::string::npos ? start : text.find(to, start);
    if (end == std::string::npos)
    {
        std::cerr << "FAIL cannot find '" << from << "' to '" << to << "'\n";
        ++failures;
        return "";
    }
    return text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::cerr << "cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const adopt = Between(text, "uint32 adoptedInside = 0;", "adopting it at {}");
    Require(adopt, "OverseerDecisions::FamilyOwnsInstance(",
            "adoption asks whether the instance is the family's");
    Require(adopt, "which is not the family's - not",
            "a refused adoption says why");

    std::string const running =
        Between(text, "A MINORITY WITH NO RUN ROW", "HOW MANY WENT IN, RATCHETED");
    Require(running, "OverseerDecisions::FamilyOwnsInstance(",
            "a running phase asks whether the instance is the family's");
    Require(running, "the instance is not the family's: the rest of the family \"", "the run ends with a reason");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "family owns instance source: all checks passed\n";
    return EXIT_SUCCESS;
}
