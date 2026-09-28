/*
 * mod-overseer#775: a "nothing is being moved" terrain note is said once per
 * place, not once per character episode. On wow-dev 2026-09-27 the same few
 * Orgrimmar streets logged "reads N yards under a surface" 1,643 times in a
 * day, mostly at ERROR, for characters the module itself said were standing on
 * the ground. These pin the cell key and that the adapter logs those two
 * notes at INFO behind it.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::TerrainNoteCell;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

std::string Slice(std::string const& text, std::string const& from, std::size_t len)
{
    std::size_t const at = text.find(from);
    return at == std::string::npos ? std::string() : text.substr(at, len);
}
}  // namespace

int main()
{
    // Zug and Zrog under the same gate roof, 30 yards apart, one cell.
    Check("one roof is one place",
          TerrainNoteCell(1, 1653.8f, -4434.5f) == TerrainNoteCell(1, 1669.8f, -4422.2f));
    Check("another street is another place",
          TerrainNoteCell(1, 1387.7f, -4365.0f) != TerrainNoteCell(1, 1653.8f, -4434.5f));
    Check("another map is another place",
          TerrainNoteCell(0, 1660.f, -4430.f) != TerrainNoteCell(1, 1660.f, -4430.f));
    Check("the sign of a coordinate matters",
          TerrainNoteCell(1, 10.f, 10.f) != TerrainNoteCell(1, -10.f, -10.f));

    std::ifstream source("src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    Check("the source was read", !text.empty());
    std::string const ground =
        Slice(text, "THE DETECTOR BEING WRONG IS NOT AN ERROR (#775)", 700);
    Check("the on-the-ground note is behind the place memory",
          ground.find("_terrainNoted.insert(") != std::string::npos);
    Check("and at INFO", ground.find("LOG_INFO(") != std::string::npos &&
                             ground.find("LOG_ERROR(") == std::string::npos);
    std::string const notFalling = Slice(text, "ONCE PER PLACE, AT INFO (#775)", 500);
    Check("the not-falling note is behind it too",
          notFalling.find("_terrainNoted.insert(") != std::string::npos &&
              notFalling.find("LOG_INFO(") != std::string::npos &&
              notFalling.find("LOG_WARN(") == std::string::npos);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a roof is noted once, at INFO\n");
    return EXIT_SUCCESS;
}
