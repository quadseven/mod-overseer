/*
 * A walk row says whether its refusal is transient (#1122), and the module
 * writes the fixture the bridge's tests import (#1123).
 *
 * The bridge used to tell a wait from a stop by string-matching the reason,
 * and `retryable` could not help: it is true for walls that merely move with
 * the character (too far, the ground, every ending). `transient` is decided in
 * OverseerDecisions, true only for the walls that clear within moments.
 *
 * The fixture, fixtures/walk_refusals.json, is WalkRefusalFixturesJson()
 * written to disk. This test fails when the file is stale and when a literal in
 * the refusal namespaces is missing from the catalogue the fixture comes from,
 * so adding a refusal without deciding its flag and regenerating stops here.
 * Regenerate from the repository root: g++ ... then
 * ./test_walk_refusal_fixtures --write
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, plus a read of
 * the module source and the fixture, so run it from the repository root.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace OverseerDecisions;
namespace M = OverseerDecisions::MailWalkRefusal;
namespace E = OverseerDecisions::ErrandWalkRefusal;
namespace S = OverseerDecisions::SpawnWalkRefusal;
namespace F = OverseerDecisions::FarWalkRefusal;

namespace
{
int failures = 0;

void Check(std::string const& what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what.c_str(), got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

std::string Slurp(char const* path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Every string literal declared in the four refusal namespaces of the header.
std::set<std::string> ScrapeLiterals(std::string const& header)
{
    std::set<std::string> found;
    for (char const* ns : {"MailWalkRefusal", "ErrandWalkRefusal", "SpawnWalkRefusal",
                           "FarWalkRefusal"})
    {
        std::size_t const begin = header.find(std::string("namespace ") + ns + "\n{");
        std::size_t const end = header.find(std::string("}  // namespace ") + ns);
        if (begin == std::string::npos || end == std::string::npos || end < begin)
        {
            std::printf("FAIL could not find namespace %s in the header\n", ns);
            ++failures;
            continue;
        }
        std::string const body = header.substr(begin, end - begin);
        std::regex const decl(R"re(constexpr char const\* \w+\s*=\s*"([^"]+)")re");
        for (std::sregex_iterator it(body.begin(), body.end(), decl), done; it != done; ++it)
            found.insert((*it)[1].str());
    }
    return found;
}
}  // namespace

int main(int argc, char** argv)
{
    char const* fixturePath = "fixtures/walk_refusals.json";
    if (argc > 1 && std::strcmp(argv[1], "--write") == 0)
    {
        std::ofstream out(fixturePath, std::ios::binary);
        out << WalkRefusalFixturesJson();
        std::printf("wrote %s\n", fixturePath);
        return out ? 0 : 1;
    }

    // The permanent gates (#1122): the row must say transient false even where
    // `retryable` says true.
    char const* const permanent[] = {
        E::TrainerTooFar, E::TrainerGround, E::NoTrainerOnMap, E::TrainerOtherSide,
        E::VendorTooFar, E::VendorGround, E::NoVendorOnMap,
        M::MailboxTooFar, M::GroundRefused, M::ExpansionContinent, M::Follower, M::Dead,
        M::InInstance, M::OnRoster, M::Malformed, S::SpawnTooFar, S::SpawnGround,
        F::BotBudgetSpent, F::RealmFull, F::NotOnAContinent,
    };
    for (char const* reason : permanent)
        Check(std::string("permanent: ") + reason, WalkRefusalTransient(reason), false);

    // The walls that clear within moments.
    char const* const transient[] = {
        M::InCombat, M::InFlight, M::LoggingOut, M::NotInWorld, M::HeldByAnother,
        M::AlreadyWalking, E::AlreadyWalking, E::TrainerCombat, E::TrainerFlight,
        E::TrainerLeftWorld, E::VendorCombat, S::SpawnCombat,
    };
    for (char const* reason : transient)
        Check(std::string("transient: ") + reason, WalkRefusalTransient(reason), true);

    // The ground-does-not-hold wall is the case the old flag got wrong: it is
    // retryable and not transient.
    Check("ground wall stays retryable", ErrandWalkRefusalRetryable(E::TrainerTooFar), true);
    Check("unknown reason is permanent", WalkRefusalTransient("something new"), false);

    // The catalogue holds every literal, once.
    std::set<std::string> const declared = ScrapeLiterals(Slurp("src/overseer_decisions.h"));
    std::set<std::string> catalogued;
    for (WalkRefusalRow const& row : WalkRefusalCatalogue())
        Check(std::string("catalogued once: ") + row.reason, catalogued.insert(row.reason).second, true);
    Check("scraped some literals", declared.size() > 40, true);
    for (std::string const& reason : declared)
        Check("declared but not catalogued: " + reason, catalogued.count(reason) == 1, true);
    for (std::string const& reason : catalogued)
        Check("catalogued but not declared: " + reason, declared.count(reason) == 1, true);

    // The fixture on disk is what the module generates now (#1123).
    std::string const onDisk = Slurp(fixturePath);
    Check("fixture exists", !onDisk.empty(), true);
    Check("fixture is current (run with --write to regenerate)",
          onDisk == WalkRefusalFixturesJson(), true);
    for (unsigned char ch : WalkRefusalFixturesJson())
        if (ch > 126 || (ch < 32 && ch != '\n'))
        {
            std::printf("FAIL fixture holds a non-ASCII byte\n");
            ++failures;
            break;
        }

    // The adapter writes the flag on the row.
    std::string const adapter = Slurp("src/mod_overseer.cpp");
    Check("the walk row carries transient",
          adapter.find("\\\"transient\\\":\" << (D::WalkRefusalTransient(reason)")
              != std::string::npos,
          true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
