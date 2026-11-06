/*
 * Source contract for the stranded split's owners and its reason line (#1143).
 *
 * The rules are proved in test_stranded_split_reason.cpp. What only the
 * adapter can get wrong is whether it asks them: the party poll must read who
 * owns a stranded member's way through WhoOwnsStrandedWay, with the crossing
 * read and the escort's owner both fed to it, and every path through
 * DriveStrandedMember that a split member takes must reach the reason line
 * before it returns. A rule nothing feeds is the failure this repository has
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

    std::string const stranded = Body(text, "bool DriveStrandedMember(");
    if (stranded.empty())
        return Fail("cannot find DriveStrandedMember");

    // WHO OWNS THE WAY, through the tested rule and fed every fact it needs.
    if (stranded.find("OverseerDecisions::WhoOwnsStrandedWay(owners)") == std::string::npos)
        return Fail("who owns a stranded member's way must be read through WhoOwnsStrandedWay");
    if (stranded.find("FamilyCrossing(family) || runEscort") != std::string::npos)
        return Fail("the old owner reading must not come back beside the rule");
    for (char const* fed :
         {"owners.familyCrossingRead = coord->second.crossingReadThisPoll",
          "owners.familyAboard = coord->second.crossingPassengers",
          "owners.escortRejoin = escort->second.rejoin",
          "owners.escortHomeBind = escort->second.homeBind",
          "owners.escortCatchUp = escort->second.catchUp",
          "owners.escortCrossing = escort->second.crossing",
          "owners.insideRun = InDungeonRun(p)", "owners.familyComesHere = familyComesHere"})
        if (stranded.find(fed) == std::string::npos)
        {
            std::cerr << "the owner rule must be fed: " << fed << "\n";
            return EXIT_FAILURE;
        }

    // THE SPLIT'S CLOCK starts and stops ahead of every return.
    std::size_t const clockStarts = stranded.find("_strandedSplitSince.emplace(name");
    std::size_t const clockStops = stranded.find("_strandedSplitSince.erase(name)");
    // The first return statement, not the word in a comment.
    std::size_t const firstReturn = stranded.find("    return ");
    if (clockStarts == std::string::npos || clockStops == std::string::npos ||
        stranded.find("_strandedReasonSaidAt.erase(name)") == std::string::npos ||
        !(clockStarts < firstReturn) || !(clockStops < firstReturn))
        return Fail("the split's clock must start with the split and stop with it, before any return");

    // THE REASON LINE on the row's path and after the verdict, before its switch.
    std::size_t const rowOwns = stranded.find("memo->second.jobRow");
    std::size_t const verdict = stranded.find("OverseerDecisions::DecideStrandedWay(facts)");
    std::size_t const acted = stranded.find("switch (way)", verdict);
    std::size_t const underRow = stranded.find("SayStrandedSplitReason(", rowOwns);
    std::size_t const afterVerdict = stranded.find("SayStrandedSplitReason(", verdict);
    if (rowOwns == std::string::npos || verdict == std::string::npos ||
        acted == std::string::npos || underRow == std::string::npos ||
        !(underRow < verdict) || afterVerdict == std::string::npos || !(afterVerdict < acted))
        return Fail("every path a split member takes through the stranded drive must reach the "
                    "reason line");
    if (stranded.find("OverseerDecisions::StrandedOwnerWhy(owner)") == std::string::npos)
        return Fail("a stand-down must name the owner that holds it");

    std::string const said = Body(text, "void SayStrandedSplitReason(");
    if (said.find("OverseerDecisions::StrandedSplitReasonDue(") == std::string::npos ||
        said.find("OverseerDecisions::StrandedSplitReasonLine(") == std::string::npos ||
        said.find("LOG_WARN(") == std::string::npos ||
        said.find("_strandedReasonSaidAt[name] = now") == std::string::npos)
        return Fail("a split past its bound must log its verdict and reason, once per bound");

    std::cout << "a stranded member's owners are read through the rule, and a split past its "
                 "bound names its verdict and reason\n";
    return EXIT_SUCCESS;
}
