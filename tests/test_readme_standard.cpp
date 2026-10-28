/*
 * README.md follows the public README standard (Refs #1188): the name on line
 * one, the seven required sections in their order, one Mermaid diagram under
 * How it works, the test commands CI really runs, one live issue under
 * Status, and ASCII only.
 *
 * The Run and test section is pinned to .github/workflows/check.decisions.yml
 * as text, both ways: each command the README names must still be in the
 * workflow, so a workflow change that drops one fails here instead of leaving
 * the README quietly wrong.
 *
 * Compiled against src/overseer_decisions.cpp like every other test; reads
 * README.md and the workflow as text (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

std::string ReadFile(char const* path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Position of a heading at the start of a line, or npos.
std::size_t Heading(std::string const& doc, std::string const& title)
{
    std::string const line = "\n" + title + "\n";
    std::size_t const at = doc.find(line);
    return at == std::string::npos ? at : at + 1;
}

// The body of one `## ` section: from its heading to the next `## `.
std::string Section(std::string const& doc, std::string const& title)
{
    std::size_t const a = Heading(doc, title);
    if (a == std::string::npos)
        return std::string();
    std::size_t const b = doc.find("\n## ", a + 1);
    return doc.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

std::vector<std::string> const kSections = {
    "## What it does",
    "## Quick start",
    "## How it works",
    "## Run and test locally",
    "## Configuration",
    "## Status",
    "## Contributing",
};

// The commands the README tells a reader to run, each of which must also be
// in the workflow that gates a pull request.
std::vector<std::string> const kCiCommands = {
    "python3 -B tests/replay_exit_hearth_finalization.py",
    "g++ -std=c++17 -Wall -Wextra -Isrc -c src/overseer_decisions.cpp -o overseer_decisions.o",
    "g++ -std=c++17 -Wall -Wextra -Isrc overseer_decisions.o \"$test\" -o \"$name\"",
};

void TheOpeningNamesTheProduct(std::string const& doc)
{
    Check("line one is the product name as a # heading",
          doc.rfind("# mod-overseer\n", 0) == 0);
}

void TheSectionsAreAllThereInOrder(std::string const& doc)
{
    std::size_t last = 0;
    for (std::string const& title : kSections)
    {
        std::size_t const at = Heading(doc, title);
        std::string const what = "section present: " + title;
        Check(what.c_str(), at != std::string::npos);
        if (at == std::string::npos)
            continue;
        std::string const order = "section in order: " + title;
        Check(order.c_str(), at > last);
        last = at;
    }

    // No extra `## ` section either: the headings outside code fences are
    // exactly kSections, in order, so a section slipped in between two
    // required ones fails here too.
    std::vector<std::string> found;
    bool fenced = false;
    std::istringstream lines(doc);
    for (std::string line; std::getline(lines, line);)
    {
        if (line.rfind("```", 0) == 0)
            fenced = !fenced;
        else if (!fenced && line.rfind("## ", 0) == 0)
            found.push_back(line);
    }
    if (found != kSections)
    {
        for (std::string const& title : found)
            std::printf("README ## heading: %s\n", title.c_str());
    }
    Check("the ## sections are exactly the seven required, in order", found == kSections);
}

void HowItWorksHasOneMermaidDiagram(std::string const& doc)
{
    std::string const how = Section(doc, "## How it works");
    std::size_t const first = how.find("```mermaid\n");
    Check("How it works has a mermaid block", first != std::string::npos);
    Check("How it works has only one mermaid block",
          first == std::string::npos || how.find("```mermaid\n", first + 1) == std::string::npos);
}

void RunAndTestMatchesTheWorkflow(std::string const& doc, std::string const& workflow)
{
    std::string const run = Section(doc, "## Run and test locally");
    Check("the workflow file was read", !workflow.empty());
    Check("Run and test names the workflow it mirrors",
          Has(run, ".github/workflows/check.decisions.yml"));
    Check("Run and test names the local runner", Has(run, "scripts/check-locally.sh"));
    for (std::string const& cmd : kCiCommands)
    {
        std::string const inReadme = "README names CI command: " + cmd;
        Check(inReadme.c_str(), Has(run, cmd));
        std::string const inWorkflow = "workflow still runs: " + cmd;
        Check(inWorkflow.c_str(), Has(workflow, cmd));
    }
}

void StatusLinksOneLiveIssue(std::string const& doc)
{
    std::string const status = Section(doc, "## Status");
    std::string const link = "https://github.com/quadseven/mod-overseer/issues/";
    std::size_t const first = status.find(link);
    Check("Status links an issue in this repo", first != std::string::npos);
    Check("Status links exactly one issue",
          first == std::string::npos || status.find(link, first + 1) == std::string::npos);
}

void ContributingPointsAtAgents(std::string const& doc)
{
    Check("Contributing points at AGENTS.md",
          Has(Section(doc, "## Contributing"), "AGENTS.md"));
}

void TheTextIsAsciiOnly(std::string const& doc)
{
    std::size_t bad = 0;
    for (unsigned char c : doc)
        if (c >= 0x80)
            ++bad;
    if (bad != 0)
        std::printf("README.md has %zu non-ASCII bytes\n", bad);
    Check("README.md is ASCII only", bad == 0);
}

} // namespace

int main()
{
    std::string const doc = ReadFile("README.md");
    std::string const workflow = ReadFile(".github/workflows/check.decisions.yml");
    Check("README.md was read", !doc.empty());

    TheOpeningNamesTheProduct(doc);
    TheSectionsAreAllThereInOrder(doc);
    HowItWorksHasOneMermaidDiagram(doc);
    RunAndTestMatchesTheWorkflow(doc, workflow);
    StatusLinksOneLiveIssue(doc);
    ContributingPointsAtAgents(doc);
    TheTextIsAsciiOnly(doc);

    if (failures != 0)
    {
        std::printf("%d README standard check(s) failed\n", failures);
        return 1;
    }
    std::printf("README standard: all checks passed\n");
    return 0;
}
