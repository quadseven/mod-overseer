// No SQL string in the module names a MySQL 8 reserved word as a bare column.
//
// On the dev realm the worldserver aborted in a crash loop (2026-09-24) on
// "SELECT name, family, lead, job, learn_skill FROM overseer_roster": LEAD is a
// window function and a reserved word in MySQL 8, so the statement is a syntax
// error, and a syntax error in a CharacterDatabase statement ends the process.
// overseer_roster really has columns named `lead` and `member`, so every
// statement that names them must quote them.

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

    // True when `word` occurs in `s` as a bare identifier: not inside
    // backticks, not part of a longer word, not after a dot.
    bool HasBare(std::string const& s, std::string const& word)
    {
        for (size_t at = s.find(word); at != std::string::npos; at = s.find(word, at + 1))
        {
            char const before = at ? s[at - 1] : ' ';
            char const after = at + word.size() < s.size() ? s[at + word.size()] : ' ';
            if (IsWordChar(before) || IsWordChar(after) || before == '`' || before == '.' || after == '`')
                continue;
            return true;
        }
        return false;
    }
}

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
    char const* const reserved[] = {"lead", "member", "rank", "rows", "groups", "window", "function"};
    char const* const verbs[] = {"SELECT ", "UPDATE ", "INSERT ", " WHERE ", " SET "};
    int statements = 0, failures = 0;
    size_t line = 1, i = 0;
    while (i < src.size())
    {
        char const c = src[i];
        if (c == '\n')
        {
            ++line;
            ++i;
            continue;
        }
        if (c != '"')
        {
            ++i;
            continue;
        }
        size_t e = i + 1;
        while (e < src.size() && src[e] != '"' && src[e] != '\n')
            e += src[e] == '\\' ? 2 : 1;
        std::string const lit = src.substr(i + 1, e - i - 1);
        bool sql = false;
        for (char const* v : verbs)
            sql = sql || lit.find(v) != std::string::npos;
        if (sql)
        {
            ++statements;
            for (char const* w : reserved)
                if (HasBare(lit, w))
                {
                    std::printf("FAIL: src/mod_overseer.cpp:%zu names reserved word '%s' unquoted: %s\n",
                                line, w, lit.c_str());
                    ++failures;
                }
        }
        i = e + 1;
    }
    if (statements < 50)
    {
        std::printf("FAIL: only %d SQL strings found; the scan is not reading the source\n", statements);
        return 1;
    }
    std::printf("%d SQL strings checked, %d unquoted reserved words\n", statements, failures);
    return failures ? 1 : 0;
}
