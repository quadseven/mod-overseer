/*
 * Source contract for the escape cap on wide columns (#1345).
 *
 * Esc() cuts its value to 255 bytes before escaping; that cap is the chat
 * length, and it is right for the VARCHAR(255) text columns it was made
 * for. Three groups of writes feed it values whose columns are wider than
 * that, so a long value was cut silently, with no log line, and a later
 * lookup by the cut key missed:
 *
 *   - overseer_dungeon_run.members, the comma-joined roster of a run
 *     (StampRunIntoCampaign, RecordUnenteredRun): a raid roster does not
 *     fit in 255 bytes;
 *   - overseer_run_recovery.options and .heuristic (WriteRecoveryRequest):
 *     the option list the bridge answers from;
 *   - overseer_loot_council.council_key: the INSERT and every lookup by
 *     key (the IN list in ReadCouncilDecisions and each UPDATE ... WHERE
 *     council_key) must cap the key the same way, or the lookup and the
 *     row it names disagree.
 *
 * Those feeds go through EscLong (the 256 KB probe cap) instead. What the
 * behaviour tests cannot see, this pins in the adapter's text: each named
 * site feeds its value through EscLong, not Esc.
 *
 * Reads src/mod_overseer.cpp as text, run from the repo root.
 */

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

bool Has(std::string const& text, std::string const& needle)
{
    return text.find(needle) != std::string::npos;
}

// Splits a call's argument text into top-level arguments (the splitter
// test_sql_text_escaped.cpp uses, kept in step with it).
std::vector<std::string> Arguments(std::string const& text)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool inString = false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char const c = text[i];
        if (inString)
        {
            cur += c;
            if (c == '\\' && i + 1 < text.size())
                cur += text[++i];
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
            inString = true;
        if (c == '(' || c == '[' || c == '{')
            ++depth;
        if (c == ')' || c == ']' || c == '}')
            --depth;
        if (c == ',' && depth == 0)
        {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    out.push_back(cur);
    for (std::string& a : out)
    {
        size_t const b = a.find_first_not_of(" \t\r\n");
        size_t const e = a.find_last_not_of(" \t\r\n");
        a = b == std::string::npos ? "" : a.substr(b, e - b + 1);
    }
    return out;
}

// Every CharacterDatabase call whose text contains `needle`, as
// (offset, call text) pairs, the call text starting after the opening '('.
std::vector<std::pair<size_t, std::string>> CallsWith(std::string const& src,
                                                      std::string const& needle)
{
    std::vector<std::pair<size_t, std::string>> out;
    std::string const opener = "CharacterDatabase.";
    for (size_t at = src.find(opener); at != std::string::npos; at = src.find(opener, at + 1))
    {
        size_t const open = src.find('(', at);
        if (open == std::string::npos)
            break;
        int depth = 1;
        bool inString = false;
        size_t i = open + 1;
        for (; i < src.size() && depth; ++i)
        {
            char const c = src[i];
            if (inString)
            {
                if (c == '\\')
                    ++i;
                else if (c == '"')
                    inString = false;
                continue;
            }
            if (c == '"')
                inString = true;
            else if (c == '(')
                ++depth;
            else if (c == ')')
                --depth;
        }
        std::string const call = src.substr(open + 1, i - open - 2);
        if (call.find(needle) != std::string::npos)
            out.emplace_back(at, call);
    }
    return out;
}

// The format string of a call: the run of adjacent literals at its start,
// and where the argument text begins.
std::string FormatOf(std::string const& call, size_t& argsAt)
{
    std::string format;
    size_t p = 0;
    while (true)
    {
        size_t const q = call.find_first_not_of(" \t\r\n", p);
        if (q == std::string::npos || call[q] != '"')
        {
            p = q;
            break;
        }
        size_t e = q + 1;
        while (e < call.size() && call[e] != '"')
            e += call[e] == '\\' ? 2 : 1;
        format += call.substr(q + 1, e - q - 1);
        p = e + 1;
    }
    argsAt = p;
    return format;
}

// The member function body that starts at `signature`, up to the next line
// that closes a member at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

}  // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string const src = ss.str();
    if (src.empty())
    {
        std::printf("FAIL: could not read src/mod_overseer.cpp\n");
        return 1;
    }

    // The run roster.
    std::string const stamp = Body(src, "    void StampRunIntoCampaign(");
    Check("StampRunIntoCampaign found", !stamp.empty());
    Check("the stamped roster goes through EscLong", Has(stamp, "EscLong(members)"));
    Check("the stamped roster no longer goes through Esc", !Has(stamp, "Esc(members)"));
    std::string const unentered = Body(src, "    void RecordUnenteredRun(");
    Check("RecordUnenteredRun found", !unentered.empty());
    Check("the recorded roster goes through EscLong", Has(unentered, "EscLong(members)"));
    Check("the recorded roster no longer goes through Esc", !Has(unentered, "Esc(members)"));

    // The recovery request's option list and heuristic word.
    std::string const recovery = Body(src, "    void WriteRecoveryRequest(");
    Check("WriteRecoveryRequest found", !recovery.empty());
    Check("the recovery options go through EscLong", Has(recovery, "EscLong(options)"));
    Check("the recovery options no longer go through Esc", !Has(recovery, "Esc(options)"));
    Check("the recovery heuristic goes through EscLong", Has(recovery, "EscLong(heuristic)"));
    Check("the recovery heuristic no longer goes through Esc", !Has(recovery, "Esc(heuristic)"));

    // The council key, at its INSERT.
    auto inserts = CallsWith(src, "INSERT IGNORE INTO overseer_loot_council");
    Check("the council INSERT is found", inserts.size() == 1);
    for (auto const& call : inserts)
        Check("the council INSERT keys through EscLong", Has(call.second, "EscLong(key)"));

    // The council key, in the decided-rows IN list.
    std::string const readBack = Body(src, "    void ReadCouncilDecisions(");
    Check("ReadCouncilDecisions found", !readBack.empty());
    Check("the council IN list keys through EscLong", Has(readBack, "EscLong(key)"));
    Check("the council IN list no longer keys through Esc", !Has(readBack, "Esc(key)"));

    // The council key, at every UPDATE ... WHERE council_key = '{}': the
    // argument feeding that placeholder starts with EscLong(.
    auto updates = CallsWith(src, "council_key = '{}'");
    Check("the council key UPDATEs are found", updates.size() >= 5);
    for (auto const& call : updates)
    {
        size_t argsAt = 0;
        std::string const format = FormatOf(call.second, argsAt);
        if (argsAt == std::string::npos || call.second[argsAt] != ',')
        {
            Check("a council UPDATE call parses", false);
            continue;
        }
        std::vector<std::string> const args = Arguments(call.second.substr(argsAt + 1));
        std::vector<size_t> holes;
        for (size_t h = format.find("{}"); h != std::string::npos; h = format.find("{}", h + 2))
            holes.push_back(h);
        size_t const keyHole = format.find("council_key = '{}'");
        size_t k = 0;
        while (k < holes.size() && holes[k] != keyHole + std::string("council_key = '").size())
            ++k;
        if (k >= holes.size() || k >= args.size())
        {
            Check("a council UPDATE's key placeholder has an argument", false);
            continue;
        }
        Check("a council UPDATE keys through EscLong", args[k].rfind("EscLong(", 0) == 0);
    }

    if (failures)
        std::printf("%d escape cap checks failed\n", failures);
    return failures ? 1 : 0;
}
