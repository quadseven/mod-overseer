/*
 * The run damage upsert quotes character names with the decisions layer's
 * own escaper, and that escaper must cover the full MySQL set (#1345).
 *
 * RunDamageUpsertSql's output is executed by the adapter as one statement.
 * The hand-rolled quoting used to escape only backslash and the single
 * quote; mysql_real_escape_string also escapes NUL, newline, carriage
 * return, the double quote and 0x1A. A name carrying any of those reached
 * the statement raw: a newline or NUL inside a quoted literal is at best a
 * corrupted row and at worst a broken statement, and a character name is
 * world text, not something this layer controls. Roster names are letters
 * only today; this pins the guard for the day one is not.
 *
 * Compiled against src/overseer_decisions.cpp, run from the repo root.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::MeterKind;
using OverseerDecisions::RunDamageBook;

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

std::string SqlFor(std::string const& name)
{
    RunDamageBook book;
    OverseerDecisions::RunDamageNote(book, name, 36, 77, MeterKind::Damage, 1);
    return OverseerDecisions::RunDamageUpsertSql(book);
}

}  // namespace

int main()
{
    // The two escapes that already worked, kept.
    Check("a quote in a name is escaped", Has(SqlFor("O'Brien"), "('O\\'Brien',36,77,1,0,0)"));
    Check("a backslash in a name is escaped", Has(SqlFor("A\\B"), "('A\\\\B',36,77,1,0,0)"));

    // The rest of the MySQL set.
    Check("a newline in a name is escaped", Has(SqlFor("A\nB"), "('A\\nB',36,77,1,0,0)"));
    Check("a carriage return in a name is escaped",
          Has(SqlFor("A\rB"), "('A\\rB',36,77,1,0,0)"));
    Check("a NUL in a name is escaped",
          Has(SqlFor(std::string("A\0B", 3)), "('A\\0B',36,77,1,0,0)"));
    Check("a 0x1A in a name is escaped", Has(SqlFor("A\x1A" "B"), "('A\\ZB',36,77,1,0,0)"));
    Check("a double quote in a name is escaped",
          Has(SqlFor("A\"B"), "('A\\\"B',36,77,1,0,0)"));

    // And none of those bytes may survive anywhere in the statement raw.
    std::string const sql = SqlFor("A\nB\rC\x1A" "D") + SqlFor(std::string("E\0F", 3));
    Check("no raw newline reaches the statement", sql.find('\n') == std::string::npos);
    Check("no raw carriage return reaches the statement", sql.find('\r') == std::string::npos);
    Check("no raw NUL reaches the statement", sql.find('\0') == std::string::npos);
    Check("no raw 0x1A reaches the statement", sql.find('\x1A') == std::string::npos);

    if (failures)
        std::printf("%d run damage escaping checks failed\n", failures);
    return failures ? 1 : 0;
}
