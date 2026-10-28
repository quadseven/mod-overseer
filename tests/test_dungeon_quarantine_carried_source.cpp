/*
 * Source contract: the end of a family dungeon run carries the coordinator's
 * failed-approach quarantine (#396) across EVERY reset, not only the re-arm
 * (#1226).
 *
 * EndRunAndDecide replaces the coordinator wholesale on each of its ways out.
 * Before #1226 only the re-arm into REPAIRING put the quarantine back; the
 * evacuated path and the repeat-wipe path (#1201) returned to IDLE with a
 * fresh coordinator, so a family that wiped twice after a quarantined approach
 * lost the quarantine, and IDLE could re-open a run with the leader still
 * standing on the refused spot.
 *
 * The coordinator type is private to the adapter, so the contract is read off
 * the source:
 *
 *   1. ResetCoordinatorKeepingQuarantine exists and carries all five fields.
 *   2. EndRunAndDecide never assigns a bare DungeonRunCoordinatorState(); every
 *      reset goes through the helper.
 *   3. The evacuated and repeat-wipe early returns each reset through the
 *      helper before they return.
 */

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

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

char const* const HELPER_CALL = "ResetCoordinatorKeepingQuarantine(coord);";

int CheckTheHelper(std::string const& text)
{
    std::string const helper =
        Body(text, "void ResetCoordinatorKeepingQuarantine(DungeonRunCoordinatorState& coord)");
    if (helper.empty())
        return Fail("cannot find ResetCoordinatorKeepingQuarantine");
    if (helper.find("DungeonRunCoordinatorState fresh;") == std::string::npos ||
        helper.find("coord = fresh;") == std::string::npos)
        return Fail("the helper must replace the coordinator with a fresh one");
    for (char const* field : {"quarantined", "quarantineX", "quarantineY", "quarantineZ",
                              "quarantineSince"})
    {
        std::string const carried =
            std::string("fresh.") + field + " = coord." + field + ";";
        if (helper.find(carried) == std::string::npos)
        {
            std::cerr << "the helper must carry " << field << "\n";
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

// The early return guarded by `guard`: the helper must be called after the
// guard and before the first return that follows it.
int CheckTheReturn(std::string const& end, char const* guard, char const* what)
{
    std::size_t const at = end.find(guard);
    if (at == std::string::npos)
    {
        std::cerr << "cannot find the " << what << " path in EndRunAndDecide\n";
        return EXIT_FAILURE;
    }
    std::size_t const reset = end.find(HELPER_CALL, at);
    std::size_t const back = end.find("return;", at);
    if (reset == std::string::npos || back == std::string::npos || reset > back)
    {
        std::cerr << "the " << what << " path must carry the quarantine before it returns\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int CheckTheEnding(std::string const& text)
{
    std::string const end = Body(text, "void EndRunAndDecide(");
    if (end.empty())
        return Fail("cannot find EndRunAndDecide");
    if (end.find("coord = DungeonRunCoordinatorState();") != std::string::npos)
        return Fail("EndRunAndDecide must not reset the coordinator without carrying the "
                    "quarantine");
    if (CheckTheReturn(end, "if (coord.evacuated)", "evacuated") != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (CheckTheReturn(end, "if (wipeEndsAttempt)", "repeat-wipe") != EXIT_SUCCESS)
        return EXIT_FAILURE;
    std::size_t const rearm = end.find("coord.phase = DungeonRunPhase::Repairing;");
    if (rearm == std::string::npos)
        return Fail("cannot find the re-arm into REPAIRING");
    std::size_t const last = end.rfind(HELPER_CALL, rearm);
    if (last == std::string::npos || last < end.find("if (wipeEndsAttempt)"))
        return Fail("the re-arm must carry the quarantine too");
    return EXIT_SUCCESS;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    if (CheckTheHelper(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (CheckTheEnding(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::cout << "every way out of EndRunAndDecide carries the failed-approach quarantine, "
                 "the evacuated and repeat-wipe returns included\n";
    return EXIT_SUCCESS;
}
