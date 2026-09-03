/*
 * What a realm says about itself, decided without a world (mod-overseer#184).
 *
 * WHY THIS SUITE IS WORTH MORE THAN ITS SIZE. Everything here is a decision
 * about strings, and every one of those decisions is wrong in a way that
 * compiles, runs, and looks right on a page. A realm kind that quietly reads a
 * typo as "not the live world" is a green banner over the live family. A pin
 * verdict that says MATCH when it cannot actually tell is a page confidently
 * printing four commit hashes that describe a different image. Neither of those
 * fails anywhere. They just print.
 *
 * THE ONE RULE UNDER ALL OF IT: an answer this module is not sure of is
 * UNKNOWN, never the reassuring one. Half the cases below exist only to pin
 * that down for a particular way of being unsure - a blank value, a typo, a
 * branch name where a commit should be, a core banner in a shape this code has
 * never seen. Every one of them must come out unknown, and the site renders
 * unknown as an alarm.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings: if a core type ever gets into one of these decisions this stops
 * building, which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using OverseerDecisions::BuildFact;
using OverseerDecisions::BuildReport;
using OverseerDecisions::CoreRevision;
using OverseerDecisions::PinsVerdict;
using OverseerDecisions::RealmKind;

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

void CheckText(std::string const& got, std::string const& want, char const* what)
{
    if (got != want)
    {
        std::printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

// The three core banners this repo can actually point at, quoted from the
// running worldservers rather than invented. The live realm and the other two
// are twelve days and one commit apart, and that gap is the reason this feature
// exists at all.
char const* const LIVE_CORE =
    "AzerothCore rev. efe123fab543+ 2026-08-14 08:34:16 -0700 (HEAD branch) "
    "(Unix, RelWithDebInfo, Static)";
char const* const NEWER_CORE =
    "AzerothCore rev. 47960183bb03+ 2026-08-28 21:04:11 +0200 (HEAD branch) "
    "(Unix, RelWithDebInfo, Static)";
// UPSTREAM-PINS.env's AC_CORE_SHA, in full, as a manifest would hand it over.
char const* const NEWER_CORE_PIN = "47960183bb03b83e8943eb2f0f39c16df9710c9d";

std::string ValueOf(std::vector<BuildFact> const& facts, std::string const& name)
{
    for (BuildFact const& fact : facts)
        if (fact.name == name)
            return fact.value;
    return std::string();
}

std::string SourceOf(std::vector<BuildFact> const& facts, std::string const& name)
{
    for (BuildFact const& fact : facts)
        if (fact.name == name)
            return fact.source;
    return std::string();
}

bool Has(std::vector<BuildFact> const& facts, std::string const& name)
{
    for (BuildFact const& fact : facts)
        if (fact.name == name)
            return true;
    return false;
}

// A deployment that sets everything, the way the worldserver manifest will once
// its own change lands.
std::map<std::string, std::string> FullEnv()
{
    return {
        {OverseerDecisions::ENV_REALM, "wow-dev"},
        {OverseerDecisions::ENV_REALM_KIND, "non-production"},
        {OverseerDecisions::ENV_PIN_CORE, NEWER_CORE_PIN},
        {OverseerDecisions::ENV_PIN_PLAYERBOTS, "2f7d9f774987d0157c6a0d0cc08c40bec3db3945"},
        {OverseerDecisions::ENV_PIN_OLLAMA_CHAT, "8ba5e791f0a84ee04636f0b19b62d3c4aff3dce1"},
        {OverseerDecisions::ENV_PIN_DUNGEON_CLEAR, "0ed117bb67148091b37541e19c1ae8e19a5260d3"},
        {OverseerDecisions::ENV_PIN_AH_BOT, "f685832994c825f90aa5a3dc0e1620aa568e875b"},
    };
}

// --- the realm kind, which is the safety-critical one ----------------------

void TestRealmKind()
{
    CheckText(RealmKind("production"), OverseerDecisions::REALM_PRODUCTION,
              "the live realm says production");
    CheckText(RealmKind("non-production"), OverseerDecisions::REALM_NON_PRODUCTION,
              "a disposable realm says non-production");

    // A YAML scalar picks these up for free and nobody ever sees them.
    CheckText(RealmKind("  PRODUCTION\n"), OverseerDecisions::REALM_PRODUCTION,
              "case and surrounding whitespace are forgiven");
    CheckText(RealmKind("Non-Production "), OverseerDecisions::REALM_NON_PRODUCTION,
              "case and surrounding whitespace are forgiven both ways");

    // EVERY ONE OF THESE IS THE POINT OF THE THREE-WAY ANSWER. A near miss must
    // not resolve to either real answer, because both mistakes are expensive:
    // reading a typo on the live realm as non-production puts a safe banner
    // over the family, and reading one on a disposable realm as production
    // trains the warning away.
    CheckText(RealmKind("prod"), OverseerDecisions::REALM_UNKNOWN,
              "an abbreviation is not production");
    CheckText(RealmKind("prd"), OverseerDecisions::REALM_UNKNOWN,
              "a typo is not production");
    CheckText(RealmKind("live"), OverseerDecisions::REALM_UNKNOWN,
              "a synonym nobody agreed on is not production");
    CheckText(RealmKind("dev"), OverseerDecisions::REALM_UNKNOWN,
              "a synonym nobody agreed on is not non-production");
    CheckText(RealmKind("nonproduction"), OverseerDecisions::REALM_UNKNOWN,
              "the hyphen is part of the word");
    CheckText(RealmKind(""), OverseerDecisions::REALM_UNKNOWN,
              "an unset realm kind is unknown, not safe");
    CheckText(RealmKind("   "), OverseerDecisions::REALM_UNKNOWN,
              "a blank realm kind is unknown, not safe");
}

// --- reading the core's own banner -----------------------------------------

void TestCoreRevision()
{
    CheckText(CoreRevision(LIVE_CORE), "efe123fab543",
              "the live realm's commit comes out of its banner");
    CheckText(CoreRevision(NEWER_CORE), "47960183bb03",
              "the newer realms' commit comes out of theirs");

    // The `+` says the tree had local modifications at build time. It is always
    // there here, because the build applies this repo's patches, and it is not
    // part of the commit.
    Check(CoreRevision(NEWER_CORE).find('+') == std::string::npos,
          "the dirty-tree marker is not part of the commit");

    // A core that changes its banner one day gets an honest empty answer rather
    // than a plausible wrong one.
    CheckText(CoreRevision("no revision in this sentence at all"), "",
              "a banner in an unknown shape yields nothing");
    CheckText(CoreRevision(""), "", "an empty banner yields nothing");
    CheckText(CoreRevision("AzerothCore rev. de+ 2026-08-14"), "",
              "two hex characters are a coincidence, not a commit");
}

// --- whether the declared pins describe this binary ------------------------

void TestPinsVerdict()
{
    CheckText(PinsVerdict(NEWER_CORE, NEWER_CORE_PIN), OverseerDecisions::PINS_MATCH,
              "a full SHA whose prefix is the running commit matches");
    CheckText(PinsVerdict(NEWER_CORE, "47960183bb03"), OverseerDecisions::PINS_MATCH,
              "an abbreviated SHA of the same length matches");
    CheckText(PinsVerdict(NEWER_CORE, "4796018"), OverseerDecisions::PINS_MATCH,
              "a declaration shorter than the banner's abbreviation still matches");
    CheckText(PinsVerdict(NEWER_CORE, "  47960183BB03  "), OverseerDecisions::PINS_MATCH,
              "case and whitespace are forgiven in a commit too");

    // THE CASE THAT IS TRUE ON THE LIVE REALM TODAY. Its image was built from
    // efe123fab543; the pins file on the default branch names 47960183. A
    // manifest that declares the branch's pins in front of that image is
    // declaring the wrong four SHAs, and this is what catches it.
    CheckText(PinsVerdict(LIVE_CORE, NEWER_CORE_PIN), OverseerDecisions::PINS_STALE,
              "pins written for a newer image than the one running are stale");

    // Unknown, not stale: an alarm about the pins would point at the wrong
    // problem when the actual fault is that the field does not hold a commit.
    CheckText(PinsVerdict(NEWER_CORE, ""), OverseerDecisions::PINS_UNKNOWN,
              "nothing declared is unknown");
    CheckText(PinsVerdict(NEWER_CORE, "Playerbot"), OverseerDecisions::PINS_UNKNOWN,
              "a branch name where a commit belongs is unknown");
    CheckText(PinsVerdict(NEWER_CORE, "4796"), OverseerDecisions::PINS_UNKNOWN,
              "four hex characters are too few to compare");
    CheckText(PinsVerdict("", NEWER_CORE_PIN), OverseerDecisions::PINS_UNKNOWN,
              "a core that did not say is unknown");
}

// --- the whole report ------------------------------------------------------

void TestReport()
{
    std::vector<BuildFact> const facts = BuildReport(NEWER_CORE, FullEnv());

    CheckText(ValueOf(facts, "module"), OverseerDecisions::VERSION,
              "the module reports its own version");
    CheckText(SourceOf(facts, "module"), OverseerDecisions::SOURCE_COMPILED,
              "the module version is compiled in");
    CheckText(ValueOf(facts, "core"), NEWER_CORE,
              "the core's whole sentence is recorded, not just the commit");
    CheckText(SourceOf(facts, "core"), OverseerDecisions::SOURCE_COMPILED,
              "the core version is compiled in");

    CheckText(ValueOf(facts, "realm"), "wow-dev", "the realm names itself");
    CheckText(SourceOf(facts, "realm"), OverseerDecisions::SOURCE_DECLARED,
              "the realm name is declared, not compiled");
    CheckText(ValueOf(facts, "realm_kind"), OverseerDecisions::REALM_NON_PRODUCTION,
              "the realm kind is the normalized answer");
    CheckText(SourceOf(facts, "realm_kind"), OverseerDecisions::SOURCE_DERIVED,
              "the realm kind is this module's reading of a declaration");

    CheckText(ValueOf(facts, "mod-playerbots"),
              "2f7d9f774987d0157c6a0d0cc08c40bec3db3945",
              "a declared upstream is recorded verbatim");
    CheckText(SourceOf(facts, "mod-ah-bot-plus"), OverseerDecisions::SOURCE_DECLARED,
              "every upstream SHA is declared");
    CheckText(ValueOf(facts, "pins"), OverseerDecisions::PINS_MATCH,
              "pins that describe this binary are recorded as matching");
    CheckText(SourceOf(facts, "pins"), OverseerDecisions::SOURCE_DERIVED,
              "the pin verdict is derived");
}

void TestReportWithNothingDeclared()
{
    // THE STATE EVERY REALM IS IN ON ITS FIRST START AFTER THIS SHIPS, because
    // the manifest that sets these variables is a separate change on a separate
    // cadence. A report from a realm that was told nothing must still be a
    // report, and must not invent a single one of the facts it was not given.
    std::vector<BuildFact> const facts =
        BuildReport(LIVE_CORE, std::map<std::string, std::string>());

    CheckText(ValueOf(facts, "module"), OverseerDecisions::VERSION,
              "an undeclared realm still reports its module version");
    CheckText(ValueOf(facts, "core"), LIVE_CORE,
              "an undeclared realm still reports its core");

    Check(!Has(facts, "realm"), "an undeclared realm name is a gap, not an empty string");
    Check(!Has(facts, "mod-playerbots"), "an undeclared upstream is a gap");
    Check(!Has(facts, "core_pin"), "an undeclared core pin is a gap");

    // THE TWO THAT ARE ALWAYS THERE. Their absence and their unknown mean
    // different things to a reader - no row at all means this realm has never
    // reported - so unknown has to be written down rather than left out.
    Check(Has(facts, "realm_kind"), "the realm kind is always reported");
    CheckText(ValueOf(facts, "realm_kind"), OverseerDecisions::REALM_UNKNOWN,
              "a realm told nothing about itself reports unknown, not safe");
    Check(Has(facts, "pins"), "the pin verdict is always reported");
    CheckText(ValueOf(facts, "pins"), OverseerDecisions::PINS_UNKNOWN,
              "a realm told no pins reports unknown, not match");
}

void TestReportKeepsTheDeclarationHonest()
{
    // A manifest that names the branch's pins in front of the live realm's
    // older image. Every declared SHA is still recorded - hiding them would
    // lose the evidence - but the verdict beside them says not to trust one.
    std::map<std::string, std::string> env = FullEnv();
    env[OverseerDecisions::ENV_REALM] = "wow";
    env[OverseerDecisions::ENV_REALM_KIND] = "production";
    std::vector<BuildFact> const facts = BuildReport(LIVE_CORE, env);

    CheckText(ValueOf(facts, "realm_kind"), OverseerDecisions::REALM_PRODUCTION,
              "the live realm reports production");
    CheckText(ValueOf(facts, "pins"), OverseerDecisions::PINS_STALE,
              "a declaration written for another image is reported stale");
    CheckText(ValueOf(facts, "mod-playerbots"),
              "2f7d9f774987d0157c6a0d0cc08c40bec3db3945",
              "the stale declaration is still recorded, so it can be read");
    CheckText(ValueOf(facts, "core_pin"), NEWER_CORE_PIN,
              "the declared core commit is kept so a reader can check the verdict");
    CheckText(ValueOf(facts, "core"), LIVE_CORE,
              "beside the core that is actually running");
}

void TestATypoNeverReadsAsSafe()
{
    // The whole feature in one case. A live realm whose manifest says "prod"
    // instead of "production" must not render as anything but an alarm.
    std::map<std::string, std::string> env = FullEnv();
    env[OverseerDecisions::ENV_REALM] = "wow";
    env[OverseerDecisions::ENV_REALM_KIND] = "prod";
    std::vector<BuildFact> const facts = BuildReport(LIVE_CORE, env);

    CheckText(ValueOf(facts, "realm_kind"), OverseerDecisions::REALM_UNKNOWN,
              "a mistyped realm kind on the live realm reads as unknown");
    Check(ValueOf(facts, "realm_kind") != OverseerDecisions::REALM_NON_PRODUCTION,
          "and never, under any spelling, as not-the-live-world");
}

}  // namespace

int main()
{
    TestRealmKind();
    TestCoreRevision();
    TestPinsVerdict();
    TestReport();
    TestReportWithNothingDeclared();
    TestReportKeepsTheDeclarationHonest();
    TestATypoNeverReadsAsSafe();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("build report: all checks passed\n");
    return EXIT_SUCCESS;
}
