/* Source contract: every ending of a guild finder run that says where the run
 * stalled also says which Zul'Farrak step it stood at (#1389).
 *
 * The words are tested in test_zf_steps.cpp and test_guild_finder_run.cpp. This
 * pins what the adapter does with them: one helper builds the stall words for
 * the timeout, the stall ladder and the module stop alike, and that helper
 * reads the run's two Zul'Farrak clocks on Zul'Farrak's map only. Before this,
 * three call sites each built "next <boss>, last kill N min ago" and a timeout
 * past three bosses read "next Hydromancer Velratha" wherever it stood.
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

std::size_t Count(std::string const& text, std::string const& needle)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + 1))
        ++n;
    return n;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const start =
        text.find("    static std::string GuildRunStallWordsOf(GuildRun const& run, std::time_t now)");
    if (start == std::string::npos)
        return Fail("GuildRunStallWordsOf(GuildRun const&, std::time_t) must build the stall words");
    std::size_t const end = text.find("\n    }\n", start);
    if (end == std::string::npos)
        return Fail("GuildRunStallWordsOf has no end");
    std::string const helper = text.substr(start, end - start);

    if (helper.find("run.mapId == OverseerDecisions::ZF_MAP_ID") == std::string::npos ||
        helper.find("OverseerDecisions::ZulFarrakStepWords(run.zfSummit, run.zfUkorz, now)") ==
            std::string::npos)
        return Fail("the helper must read the two Zul'Farrak clocks, on Zul'Farrak's map only");
    if (helper.find("OverseerDecisions::GuildRunStallWords(") == std::string::npos ||
        helper.find("run.nextBoss") == std::string::npos ||
        helper.find("run.lastKillAt") == std::string::npos)
        return Fail("the helper must keep the next boss and the last kill");

    if (Count(text, "OverseerDecisions::GuildRunStallWords(") != 1)
        return Fail("the stall words must be built in GuildRunStallWordsOf and nowhere else");
    if (Count(text, "GuildRunStallWordsOf(run, now)") != 3)
        return Fail("the timeout, the stall ladder and the module stop must each use the helper");

    std::size_t const timedOut =
        text.find("case OverseerDecisions::GuildRunVerdict::TimedOut:\n"
                  "                        why = OverseerDecisions::GuildRunTimedOutWhy(\n"
                  "                            poll.secondsInside, GuildRunStallWordsOf(run, now));");
    if (timedOut == std::string::npos)
        return Fail("the timed-out why must carry the helper's words");

    std::cout << "zf timeout step source: ok\n";
    return EXIT_SUCCESS;
}
