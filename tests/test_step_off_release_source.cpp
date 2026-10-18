/*
 * Source contract for the step off a docked deck (#279).
 *
 * The decision to let a passenger go is proved in the crossing tests
 * (ShouldLeaveDeck, PickLanding). What only the adapter can get wrong is HOW it
 * lets him go. MotionTransport::RemovePassenger(passenger, withAll = false)
 * only erases the passenger from the transport's set; SetTransport(nullptr) and
 * clearing MOVEMENTFLAG_ONTRANSPORT happen only when withAll is true. The core
 * and the bot AI's own transport check (PlayerbotAI::UpdateAI) both pass true.
 *
 * On the dev realm on 2026-10-18 a member reached the far dock, was released
 * with the one-argument call ten polls running ("release 1" to "release 10"),
 * read as still aboard every time because GetTransport() still named the boat,
 * then ran out of landings ("no surveyed ground off its deck within 80 yards")
 * and its cross-to-map row timed out. The bot AI's check could not repair it:
 * its own RemovePassenger(bot, true) found nothing left to erase, so the
 * withAll half never ran. A release that leaves the transport pointer set is
 * the failure, so every release in the adapter is pinned as text.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(std::string const& what)
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

// The argument list of the call whose name ends at `open` (the '(' itself).
std::string Arguments(std::string const& text, std::size_t open)
{
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i)
    {
        if (text[i] == '(')
            ++depth;
        else if (text[i] == ')' && --depth == 0)
            return text.substr(open + 1, i - open - 1);
    }
    return std::string();
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    // EVERY RELEASE PASSES withAll. A one-argument RemovePassenger anywhere in
    // the adapter leaves GetTransport() set on a character nothing carries.
    std::string const call = "RemovePassenger(";
    int releases = 0;
    for (std::size_t at = text.find(call); at != std::string::npos;
         at = text.find(call, at + call.size()))
    {
        ++releases;
        std::string const args = Arguments(text, at + call.size() - 1);
        std::size_t const comma = args.find(',');
        if (comma == std::string::npos || args.find("true", comma) == std::string::npos)
            return Fail("RemovePassenger(" + args +
                        ") must pass withAll = true, or GetTransport() still names the "
                        "transport after the release");
    }

    // AND THE STEP OFF IS ONE OF THEM: StepOntoGround is where a passenger
    // beside the far dock is let go of, and it must still do so.
    std::string const step = Body(text, "float StepOntoGround(");
    if (step.empty())
        return Fail("cannot find StepOntoGround");
    std::size_t const decide = step.find("OverseerDecisions::ShouldLeaveDeck(");
    std::size_t const release = step.find(call, decide == std::string::npos ? 0 : decide);
    if (decide == std::string::npos || release == std::string::npos)
        return Fail("StepOntoGround must release the passenger when ShouldLeaveDeck says so");
    if (releases == 0)
        return Fail("no RemovePassenger call found at all");

    std::cout << "step off release source contract: " << releases
              << " release(s), all with withAll\n";
    return EXIT_SUCCESS;
}
