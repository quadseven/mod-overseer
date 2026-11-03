/*
 * Source contract for the command row write-back (#1345).
 *
 * The write-back of a claimed overseer_command row - status, detail and
 * result, guarded by the row still being this process's claim - used to be
 * spelled out at every command that finished one: thirty-one copies of
 * the same UPDATE, each applying the Esc/EscLong discipline by hand, so a
 * fix to the discipline had thirty-one places to reach. It now goes
 * through one helper, WriteCommandRow (a detail-only overload covers the
 * finishes that write no result), next to Esc and EscLong, and
 * WriteGuildRunRow - the shape the helper was modelled on - delegates to
 * it like every other finish.
 *
 * Two statements that mention the same table text are NOT write-backs and
 * stay spelled out, and this test names them so the exemption is a list,
 * not a mood: the claim stamp itself (SET status = 'claimed',
 * claimed_by = '{}', WHERE status = 'pending'), and the claim-expiry
 * release (SET ... claimed_by = '' WHERE status IN ('claimed',
 * 'verifying')), which un-claims a dead claim instead of finishing it.
 *
 * What this pins: WriteCommandRow exists (both overloads) and holds the
 * only raw write-back text; every other CharacterDatabase statement
 * carrying the write-back's UPDATE is one of the two named statements;
 * and the finishes call the helper, in number.
 *
 * Reads src/mod_overseer.cpp as text, run from the repo root.
 */

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
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

// The body of the definition that starts at `at` (a signature): from the
// first '{' to its matching '}', string-aware.
std::pair<size_t, size_t> BodySpan(std::string const& src, size_t at)
{
    size_t const open = src.find('{', at);
    if (open == std::string::npos)
        return {std::string::npos, std::string::npos};
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
        else if (c == '{')
            ++depth;
        else if (c == '}')
            --depth;
    }
    return {open, i};
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

    // The helper, both overloads, and the spans their bodies cover.
    std::vector<std::pair<size_t, size_t>> helperSpans;
    size_t definitions = 0;
    for (size_t at = src.find("void WriteCommandRow("); at != std::string::npos;
         at = src.find("void WriteCommandRow(", at + 1))
    {
        // A definition is followed by a '{' before the next ';' would end a
        // declaration; these are definitions, written once, at file scope.
        auto const span = BodySpan(src, at);
        if (span.first != std::string::npos)
        {
            helperSpans.push_back(span);
            ++definitions;
        }
    }
    Check("WriteCommandRow is defined in both overloads", definitions == 2);
    auto inHelper = [&helperSpans](size_t at) {
        for (auto const& span : helperSpans)
            if (at >= span.first && at <= span.second)
                return true;
        return false;
    };

    // Every CharacterDatabase statement carrying the write-back's table
    // text: inside the helper, the claim stamp, or the expiry release -
    // anything else is a raw copy that escaped the consolidation.
    int rawSeen = 0, exemptSeen = 0;
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
        if (!Has(call, "UPDATE overseer_command SET status"))
            continue;
        ++rawSeen;
        if (inHelper(open))
            continue;
        if (Has(call, "status = 'claimed', claimed_by") || Has(call, "claimed_by = ''"))
        {
            ++exemptSeen;
            continue;
        }
        size_t const line = static_cast<size_t>(
                                std::count(src.begin(), src.begin() + static_cast<long>(at), '\n')) +
                            1;
        std::printf("FAIL: raw command write-back outside WriteCommandRow at line %zu\n", line);
        ++failures;
    }
    Check("the scan found the write-back text at all", rawSeen >= 6);
    Check("both named exemptions are still present", exemptSeen == 2);

    // The finishes call the helper: the two definitions account for two of
    // the occurrences of its name as a call; the rest are the finishes
    // (thirty-one on the day this was written - the floor leaves room for
    // a finish to be retired, not for the copies to come back).
    size_t calls = 0;
    for (size_t at = src.find("WriteCommandRow("); at != std::string::npos;
         at = src.find("WriteCommandRow(", at + 1))
        ++calls;
    Check("the finishes go through WriteCommandRow", calls - definitions >= 28);

    if (failures)
        std::printf("%d command row write-back checks failed\n", failures);
    return failures ? 1 : 0;
}
