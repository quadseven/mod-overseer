/*
 * The pre-run gate must refuse an overhead approach and identify the measured
 * position in its warning, so an operator can see which location was held.
 */

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream input("src/mod_overseer.cpp");
    if (!input)
    {
        std::cerr << "cannot read src/mod_overseer.cpp\n";
        return 1;
    }
    std::string const source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (source.find("dungeon run held - the '{}' {} is ") == std::string::npos ||
        source.find("at `({:.1f}, {:.1f}, {:.1f})`") == std::string::npos ||
        source.find("if (shape == OverseerDecisions::ApproachShape::Overhead)") ==
            std::string::npos)
    {
        std::cerr << "overhead refusal does not log its refused position\n";
        return 1;
    }
    return 0;
}
