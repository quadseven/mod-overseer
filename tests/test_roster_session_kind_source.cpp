/*
 * Source contract for the roster sweep's session sort (#134).
 *
 * The decision is proved in test_roster_session_kind.cpp. What only the
 * adapter can get wrong is whether it ASKS it: KeepRosterAttended has to feed
 * the session's facts to ClassifyRosterSession and act on the kind it gets,
 * rather than keep its own inline copy of the sort that could drift from the
 * tested one. A decision nothing feeds is the failure this repository has
 * shipped before, so the wiring is pinned as text.
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
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const sweep = Body(text, "void KeepRosterAttended()");
    if (sweep.empty())
        return Fail("cannot find KeepRosterAttended");

    std::size_t const asks = sweep.find("OverseerDecisions::ClassifyRosterSession(");
    if (asks == std::string::npos)
        return Fail("KeepRosterAttended must sort a session with ClassifyRosterSession");

    for (char const* fed : {"sessionFacts.clientAttached = ClientAttached(player)",
                            "sessionFacts.hasSession = session != nullptr",
                            "sessionFacts.isBotSession = session && OverseerCompat::SessionIsHeadless(*session)",
                            "sessionFacts.alreadyKicked = session && session->IsKicked()"})
        if (sweep.find(fed) == std::string::npos)
        {
            std::cerr << "KeepRosterAttended must feed the sort: " << fed << "\n";
            return EXIT_FAILURE;
        }

    // Each kind is acted on, and only after the sort has been asked.
    std::size_t const live = sweep.find("RosterSessionKind::LiveClient", asks);
    std::size_t const settling = sweep.find("RosterSessionKind::Settling", asks);
    std::size_t const headless = sweep.find("RosterSessionKind::HeadlessBot", asks);
    std::size_t const kick = sweep.find("session->KickPlayer(", asks);
    if (live == std::string::npos || settling == std::string::npos ||
        headless == std::string::npos || kick == std::string::npos)
        return Fail("KeepRosterAttended must act on every kind the sort returns, after asking it");
    if (!(live < headless && settling < headless && headless < kick))
        return Fail("a live client and a settling session must be left before any eviction or kick");

    // No inline copy of the sort is left behind to drift from the tested one.
    if (sweep.find("if (ClientAttached(player))") != std::string::npos ||
        sweep.find("if (session->IsBot())") != std::string::npos ||
        sweep.find("if (OverseerCompat::SessionIsHeadless(*session))") != std::string::npos ||
        sweep.find("if (session->IsKicked())") != std::string::npos)
        return Fail("KeepRosterAttended must not keep its own inline copy of the session sort");

    std::cout << "the roster sweep sorts every session with ClassifyRosterSession\n";
    return EXIT_SUCCESS;
}
