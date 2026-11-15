/*
 * Source contract for config reads the realm never sets.
 *
 * The core's GetOption warns "Config: Missing property <key> ..." on every
 * call when the key is absent from the config file and showLogs, the third
 * argument, is left at its default of true. The six keys below are read on
 * every raid pass and every route plan, and the realm leaves them at their
 * defaults on purpose. Read without `false`, one line of the worldserver log
 * in every two was that warning (5,690 of 9,812 lines in 30 minutes on the
 * dev realm), which buried the lines that matter.
 *
 * What this file pins: each read of these six keys passes `false` as the third
 * argument, and keeps the default it had. The conf template must still
 * document each key, so turning one on later stays a one-line change.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(bool ok, std::string const& what)
{
    if (!ok)
    {
        std::cerr << "FAIL " << what << "\n";
        ++failures;
    }
}

std::string Read(char const* path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The text from `start` to the next semicolon with every run of whitespace
// removed, so a call split over two lines compares the same as one on a line.
std::string Statement(std::string const& text, std::size_t start)
{
    std::size_t const end = text.find(';', start);
    std::string out;
    if (end == std::string::npos)
        return out;
    for (std::size_t i = start; i <= end; ++i)
        if (text[i] != ' ' && text[i] != '\n' && text[i] != '\r' && text[i] != '\t')
            out += text[i];
    return out;
}

struct Read6
{
    char const* key;
    char const* defaultText;
};
}  // namespace

int main()
{
    std::string const text = Read("src/mod_overseer.cpp");
    if (text.empty())
    {
        std::cerr << "FAIL cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }
    std::string const dist = Read("conf/mod_overseer.conf.dist");
    if (dist.empty())
    {
        std::cerr << "FAIL cannot read conf/mod_overseer.conf.dist\n";
        return EXIT_FAILURE;
    }

    std::vector<Read6> const reads = {
        {"Overseer.Raid.QuorumSeats", "OverseerDecisions::RAID_QUORUM_SEATS"},
        {"Overseer.Raid.QuorumTanks", "OverseerDecisions::RAID_QUORUM_TANKS"},
        {"Overseer.Raid.QuorumHealers", "OverseerDecisions::RAID_QUORUM_HEALERS"},
        {"Overseer.Raid.RepairFloorPct", "70"},
        {"Overseer.Route.NeutralElites", "true"},
        {"Overseer.Route.LoneGreyDensity", "0"},
    };

    for (Read6 const& r : reads)
    {
        std::string const quoted = std::string("\"") + r.key + "\"";
        std::size_t at = 0;
        int calls = 0;
        while ((at = text.find(quoted, at)) != std::string::npos)
        {
            // A read is a GetOption call; the key also appears in log text.
            std::size_t const open = text.rfind("GetOption<", at);
            bool const isCall = open != std::string::npos && at - open < 80 &&
                                text.find(';', open) > at;
            if (isCall)
            {
                ++calls;
                std::string const stmt = Statement(text, open);
                std::string const quiet = std::string(quoted) + "," + r.defaultText + ",false)";
                Check(stmt.find(quiet) != std::string::npos,
                      std::string(r.key) + " is read with its default and showLogs false: " + stmt);
            }
            at += quoted.size();
        }
        Check(calls >= 1, std::string(r.key) + " is still read through GetOption");
        Check(dist.find(std::string("#") + r.key + " =") != std::string::npos,
              std::string(r.key) + " is still documented in the conf template");
    }

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
