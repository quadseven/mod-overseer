/*
 * Source contract for a food vendor walk taken in the revived sickness hold
 * (mod-overseer#1377).
 *
 * SupplyWalkReleasesSickHold is proved in test_revived_sick_vendor_walk.cpp.
 * What only the adapter can get wrong:
 *   - DoWalk must ask the rule, and release the sickness hold under its own
 *     verb, before it reads heldByAnother and asks MailWalkGate. Asked after,
 *     the gate has already refused the walk.
 *   - DriveRevivedSickness re-asserts its stillness hold every poll. While a
 *     walk hold owns the member it must not, or the next poll stops the walk.
 *   - The release must not go through ReleaseRevivedSickness, which hands the
 *     combat strategies back: the member stays out of combat on the way.
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

// The body of the member function that opens at `signature`: from it to the
// next line that closes a member at the class's indent.
std::string BodyOf(std::string const& text, std::string const& signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return "";
    std::size_t const end = text.find("\n    }\n", start);
    if (end == std::string::npos)
        return "";
    return text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const walk = BodyOf(text, "static char const* DoWalk(Player* who,");
    if (walk.empty())
        return Fail("cannot find DoWalk");
    std::size_t const rule = walk.find("D::SupplyWalkReleasesSickHold(");
    if (rule == std::string::npos)
        return Fail("DoWalk must ask SupplyWalkReleasesSickHold (#1377)");
    std::size_t const release = walk.find("REVIVED_SICK_HOLD_VERB);", rule);
    if (release == std::string::npos || walk.rfind("ReleaseHold(", release) < rule)
        return Fail("DoWalk must release the sickness hold under its own verb after the rule "
                    "says so (#1377)");
    std::size_t const held = walk.find("gate.heldByAnother =");
    std::size_t const gate = walk.find("if (char const* wall = D::MailWalkGate(gate); *wall)");
    if (held == std::string::npos || gate == std::string::npos)
        return Fail("cannot find DoWalk's heldByAnother or MailWalkGate");
    if (!(rule < held && release < held && held < gate))
        return Fail("DoWalk must release the sickness hold before heldByAnother and "
                    "MailWalkGate are read (#1377)");
    if (walk.find("ReleaseRevivedSickness(", rule) != std::string::npos)
        return Fail("DoWalk must not call ReleaseRevivedSickness: it restores combat (#1377)");

    // The drive defers to MAIL_WALK_HOLD_VERB because that is the verb a walk
    // of every goal, the vendor walk included, holds under for its journey.
    if (walk.find("HoldStillAndReport(who, ev.character, MAIL_WALK_HOLD_VERB, ev.hold,\n"
                  "                           ev.timeoutMs") == std::string::npos)
        return Fail("DoWalk must hold the walk under MAIL_WALK_HOLD_VERB, the verb "
                    "DriveRevivedSickness defers to (#1377)");

    std::string const drive = BodyOf(text, "void DriveRevivedSickness(Player* bot,");
    if (drive.empty())
        return Fail("cannot find DriveRevivedSickness");
    std::size_t const still = drive.find("HoldCharacterStill(bot, botAI, name, REVIVED_SICK_HOLD_VERB");
    if (still == std::string::npos)
        return Fail("cannot find DriveRevivedSickness's stillness hold");
    std::size_t const defer = drive.find("MAIL_WALK_HOLD_VERB");
    if (defer == std::string::npos || defer > still)
        return Fail("DriveRevivedSickness must defer to a walk hold before it holds the "
                    "member still (#1377)");

    // A walk paused for a fight has let go of its hold (#633); the drive must
    // see that walk too, or it takes the member back and the walk ends "held
    // by another verb" when the fight is over (#1377, live 2026-11-08).
    std::size_t const asks = drive.find("RevivedSicknessDefersToWalk(");
    if (asks == std::string::npos || asks > still)
        return Fail("DriveRevivedSickness must ask RevivedSicknessDefersToWalk before it holds "
                    "the member still (#1377)");
    std::size_t const paused = drive.find(".ev.paused");
    if (paused == std::string::npos || paused > asks ||
        drive.find("_pendingMailWalks") == std::string::npos)
        return Fail("DriveRevivedSickness must read whether the member's walk in "
                    "_pendingMailWalks is paused for a fight (#1377)");

    std::cout << "revived sickness vendor walk source: contract holds\n";
    return EXIT_SUCCESS;
}
