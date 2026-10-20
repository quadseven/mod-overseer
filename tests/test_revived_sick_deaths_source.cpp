/*
 * Source contract for a sick revival's own death count (mod-overseer#746).
 *
 * NoteRevivedSickDeath is proved in test_revived_sick_ground.cpp. What only
 * the adapter can get wrong is whether a death REACHES it: Resurrection
 * Sickness outlives a death, so the drive's state for one sickness lives
 * through every death inside its ten minutes, and before this the count read
 * at the first revival stood for all of them. Og died about every two minutes
 * at the Badlands NE spirit healer on wow-dev 2026-10-20 with "0 death(s)
 * here" for the whole loop. Each drive that calls DriveRevivedSickness on the
 * living must note the death on the dead.
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

// The dead branch of a drive opens right after its alive branch, which ends
// on `DriveRevivedSickness(<call>);` and `continue;`. The note must come
// before anything else the dead branch does.
bool NotesDeathAfter(std::string const& text, std::size_t drive, std::string const& call)
{
    std::size_t const alive = text.find(call, drive);
    if (alive == std::string::npos)
        return false;
    std::string const closing = "continue;\n            }\n";
    std::string const note = "            NoteRevivedSickDeath(name);\n";
    std::size_t const close = text.find(closing, alive);
    if (close == std::string::npos)
        return false;
    return text.compare(close + closing.size(), note.size(), note) == 0;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const family = text.find("void DriveStuckRevival()");
    if (family == std::string::npos)
        return Fail("cannot find DriveStuckRevival");
    if (!NotesDeathAfter(text, family, "DriveRevivedSickness(bot, botAI, name);"))
        return Fail("DriveStuckRevival's dead branch must open with NoteRevivedSickDeath (#746)");

    std::size_t const guild = text.find("void DriveGuildGhostRecovery()");
    if (guild == std::string::npos)
        return Fail("cannot find DriveGuildGhostRecovery");
    if (!NotesDeathAfter(text, guild,
                         "DriveRevivedSickness(bot, botAI, name, /*restWhileSick*/ true);"))
        return Fail("DriveGuildGhostRecovery's dead branch must open with NoteRevivedSickDeath "
                    "(#746)");

    std::size_t const note = text.find("void NoteRevivedSickDeath(std::string const& name)");
    if (note == std::string::npos)
        return Fail("cannot find the adapter's NoteRevivedSickDeath");
    std::size_t const noteEnd = text.find("\n    }\n", note);
    if (text.substr(note, noteEnd - note)
            .find("OverseerDecisions::NoteRevivedSickDeath(it->second.deaths);") ==
        std::string::npos)
        return Fail("the adapter's NoteRevivedSickDeath must clear the state's count (#746)");

    std::size_t const drive = text.find("void DriveRevivedSickness(");
    if (drive == std::string::npos)
        return Fail("cannot find DriveRevivedSickness");
    std::size_t const decide = text.find("DecideRevivedSickGround(facts)", drive);
    std::string const body = text.substr(drive, decide - drive);
    if (body.find("if (!state.deaths.read)") == std::string::npos ||
        body.find("facts.deathsHere = state.deaths.here;") == std::string::npos)
        return Fail("DriveRevivedSickness must read its deaths again once a death clears "
                    "the read (#746)");

    std::cout << "revived sick deaths source: all checks passed\n";
    return EXIT_SUCCESS;
}
