// Every free-text value spliced into a quoted SQL placeholder is escaped.
//
// On the dev realm the worldserver aborted three times in ten minutes
// ("Error while parsing SQL. Core fix required.", MySQL 1064) because a
// mailbox walk wrote its refusal reason, "the way to the nearest mailbox
// crosses the other side's ground", into `detail = '{}'` unescaped. The
// apostrophe closed the string literal, and a syntax error in a
// CharacterDatabase statement is fatal to the whole process.
//
// This reads src/mod_overseer.cpp and, for every CharacterDatabase statement
// whose format string has a `detail = '{}'`, `reason = '{}'` or
// `outcome = '{}'` placeholder, checks that the argument feeding it is
// wrapped in Esc( or EscLong(.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string ReadSource()
    {
        std::ifstream in("src/mod_overseer.cpp");
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Splits a call's text after the format string into top-level arguments.
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
}

int main()
{
    std::string const src = ReadSource();
    if (src.empty())
    {
        std::printf("FAIL: could not read src/mod_overseer.cpp\n");
        return 1;
    }
    char const* const watched[] = {"detail = '{}'", "reason = '{}'", "outcome = '{}'"};
    int checked = 0, failures = 0;
    std::string const opener = "CharacterDatabase.";
    for (size_t at = src.find(opener); at != std::string::npos; at = src.find(opener, at + 1))
    {
        size_t const open = src.find('(', at);
        if (open == std::string::npos)
            break;
        // The call's full text, to its matching parenthesis, skipping strings.
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
        // The format is the run of adjacent string literals at the start.
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
        if (p == std::string::npos || call[p] != ',')
            continue;
        std::vector<std::string> const args = Arguments(call.substr(p + 1));
        // Placeholder k of the format feeds argument k.
        std::vector<size_t> holes;
        for (size_t h = format.find("{}"); h != std::string::npos; h = format.find("{}", h + 2))
            holes.push_back(h);
        for (char const* w : watched)
        {
            std::string const needle(w);
            for (size_t h = format.find(needle); h != std::string::npos; h = format.find(needle, h + 1))
            {
                size_t const hole = h + needle.size() - 3;
                size_t k = 0;
                while (k < holes.size() && holes[k] != hole)
                    ++k;
                if (k >= holes.size() || k >= args.size())
                    continue;
                ++checked;
                std::string const& a = args[k];
                bool const escaped = a.rfind("Esc(", 0) == 0 || a.rfind("EscLong(", 0) == 0;
                if (!escaped)
                {
                    size_t const line = static_cast<size_t>(
                        std::count(src.begin(), src.begin() + static_cast<long>(at), '\n')) + 1;
                    std::printf("FAIL: src/mod_overseer.cpp:%zu `%s` is fed by unescaped `%s`\n",
                                line, w, a.c_str());
                    ++failures;
                }
            }
        }
    }
    if (checked < 10)
    {
        std::printf("FAIL: only %d placeholders found; the scan is not reading the source\n", checked);
        return 1;
    }
    std::printf("%d placeholders checked, %d unescaped\n", checked, failures);
    return failures ? 1 : 0;
}
