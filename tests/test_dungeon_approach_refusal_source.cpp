/*
 * The pre-run gate must refuse an overhead approach and identify the measured
 * position in its warning, so an operator can see which location was held.
 *
 * And the quarantine a failed approach sets (#396) must outlive the process:
 * it is written to overseer_approach_quarantine when FailApproach closes the
 * attempt, read back by the IDLE gate before it decides, and deleted only when
 * the pure decision clears it. A restart between the failure and the next poll
 * used to forget it and re-open the run from the same rim.
 */

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{

int failures = 0;

bool Read(char const* path, std::string& out)
{
    std::ifstream input(path);
    if (!input)
    {
        std::cerr << "cannot read " << path << "\n";
        ++failures;
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return true;
}

void Require(std::string const& text, char const* needle, char const* why)
{
    if (text.find(needle) != std::string::npos)
        return;
    std::cerr << "FAIL " << why << ": missing `" << needle << "`\n";
    ++failures;
}

}  // namespace

int main()
{
    std::string source;
    std::string schema;
    if (Read("src/mod_overseer.cpp", source))
    {
        Require(source, "if (shape == OverseerDecisions::ApproachShape::Overhead)",
                "the overhead gate refuses the run");
        Require(source, "dungeon run held - the '{}' {} is {} and the ",
                "the held line names the corridor point");
        Require(source, "leader '{}' at {} is {}. That ",
                "the held line names the refused leader position");
        Require(source, "OverseerDecisions::BacktickedPosition(first.x, first.y, first.z)",
                "the held line backticks the corridor point");
        Require(source, "INSERT INTO overseer_approach_quarantine",
                "FailApproach persists the quarantine");
        Require(source, "FROM overseer_approach_quarantine WHERE family",
                "the IDLE gate reads the persisted quarantine back");
        Require(source, "DELETE FROM overseer_approach_quarantine WHERE family",
                "a cleared quarantine is removed from the table");
        Require(source, "PersistApproachQuarantine(family, leaderName, coord)",
                "FailApproach writes the row for its family");
        Require(source, "LoadApproachQuarantine(family, coord)",
                "the IDLE gate loads the row before deciding");
    }
    if (Read("data/sql/characters/base/2026_10_25_00_overseer_approach_quarantine.sql",
             schema))
        Require(schema, "CREATE TABLE IF NOT EXISTS `overseer_approach_quarantine`",
                "the quarantine table ships with the module");
    return failures == 0 ? 0 : 1;
}
