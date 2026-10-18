/*
 * Source contract for what the terrain drive's lines say (#188, fix C).
 *
 * 1. The two give-up lines print a position taken from AimSnapshot, which
 *    RememberAim fills with the CHARACTER'S OWN position at the moment the
 *    aim was read. They used to label it "last aimed position", and a reader
 *    takes that for the aim's target, which is a different place and is
 *    already printed as travel='...'. So the label says what the number is.
 *
 * 2. The below-the-world line measures the floor under the character's feet
 *    and used to print only whether one was found. The surface overhead and
 *    the floor below, side by side, are the two readings that separate
 *    "under a roof" from "in a void", so the floor is printed as a number.
 *
 * Read as text, like the other *_source tests, because the lines live in the
 * adapter and cannot be compiled without a core.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

std::size_t Count(std::string const& text, std::string const& needle)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size()))
        ++n;
    return n;
}
} // namespace

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

    Check(text.find("last aimed position") == std::string::npos,
          "no line calls the character's own position the 'last aimed position'");
    Check(Count(text, "read while it stood at map {} (") == 2,
          "both give-up lines say the position is where the character stood "
          "when the aim was read");
    Check(text.find("seenX = it->second.x;") != std::string::npos,
          "the snapshot position is carried under a name that says what it is");

    std::string const branch =
        "if (verdict.remedy == OverseerDecisions::TerrainRemedy::LiftToSurface)\n"
        "            {";
    std::size_t const start = text.find(branch);
    std::size_t const line =
        start == std::string::npos
            ? std::string::npos
            : text.find("reads as below the world at map {} position", start);
    std::size_t const end =
        line == std::string::npos ? std::string::npos : text.find("continue;", line);
    Check(start != std::string::npos && line != std::string::npos &&
              end != std::string::npos,
          "the below-the-world branch of the terrain drive is where it was");
    if (end != std::string::npos)
    {
        std::string const body = text.substr(start, end - start);
        Check(body.find("<< reading.floorBelowZ") != std::string::npos,
              "the below-the-world line prints the floor z it measured");
        Check(body.find("floorBelow,") != std::string::npos,
              "the floor text is an argument of that line");
    }

    if (failures)
        return EXIT_FAILURE;
    std::cout << "terrain lines name the position they print and the floor they measured\n";
    return EXIT_SUCCESS;
}
