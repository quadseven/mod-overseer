/*
 * Source contract: during a raid's ASSEMBLE the run is the only writer of a
 * seat's travel aim, and a seat the module cannot steer is held with `+stay`.
 *
 * In a raid rehearsal the follow drive's catch-up walk and the run's staging
 * walk both wrote the same seats' aims, so the aim flipped between the leader's
 * position and the door, and seats the module cannot steer (no client) were
 * pulled along by upstream's own follow until the lava. What this file pins:
 *
 *  - CatchUpToward refuses to claim for a member whose raid run is in ASSEMBLE;
 *  - so does the catch-up branch that renews an existing aim, which claims
 *    directly and does not go through CatchUpToward;
 *  - DriveRaidSeatWalks gives a seat it cannot steer `+stay`, and takes it off
 *    again when the seat reaches the door or ASSEMBLE ends, only if it put it
 *    there.
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
    std::cerr << "FAIL " << what << "\n";
    return EXIT_FAILURE;
}

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

    std::string const holds = Body(text, "    bool RaidAssembleHolds(");
    if (holds.empty())
        return Fail("cannot find RaidAssembleHolds");
    if (holds.find("_raidRunPhaseByMember") == std::string::npos ||
        holds.find("RaidRunPhase::Assemble") == std::string::npos)
        return Fail("RaidAssembleHolds must read the member's raid phase and ask for Assemble");

    std::string const catchUp = Body(text, "    bool CatchUpToward(");
    if (catchUp.empty())
        return Fail("cannot find CatchUpToward");
    std::size_t const guard = catchUp.find("RaidAssembleHolds(name)");
    std::size_t const claim = catchUp.find("_travelAims.Claim(");
    if (guard == std::string::npos)
        return Fail("CatchUpToward must ask RaidAssembleHolds");
    if (claim == std::string::npos || guard > claim)
        return Fail("CatchUpToward must ask RaidAssembleHolds before it claims");

    // The renewal branch of DriveCatchUp claims with the CatchUp owner directly.
    std::size_t const renew = text.find("else if (!_travelAims.Claim(name, it->second.aim,");
    if (renew == std::string::npos)
        return Fail("cannot find the catch-up renewal claim");
    std::size_t const renewFn = text.rfind("void DriveCatchUp(", renew);
    std::size_t const renewGuard = text.rfind("RaidAssembleHolds(name)", renew);
    if (renewFn == std::string::npos || renewGuard == std::string::npos || renewGuard < renewFn)
        return Fail("the catch-up renewal must not claim while ASSEMBLE holds the member");

    std::string const walks = Body(text, "    void DriveRaidSeatWalks(");
    if (walks.empty())
        return Fail("cannot find DriveRaidSeatWalks");
    if (walks.find("SteerableAI(member)") == std::string::npos)
        return Fail("DriveRaidSeatWalks must ask whether the seat can be steered");
    if (walks.find("\"+stay\"") == std::string::npos)
        return Fail("a seat the module cannot steer must get +stay");
    std::string const release = Body(text, "    void ReleaseStayedSeat(");
    if (release.find("\"-stay\"") == std::string::npos)
        return Fail("the +stay must be taken off again");
    if (walks.find("ReleaseStayedSeat(run, name)") == std::string::npos ||
        walks.find("ReleaseStayedSeats(run)") == std::string::npos)
        return Fail("the +stay must be released at the door and when ASSEMBLE ends");
    if (walks.find("seatsStayed") == std::string::npos)
        return Fail("the seats held with +stay must be remembered, so only those are released");

    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
