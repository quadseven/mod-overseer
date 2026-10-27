/*
 * A dungeon run's ended_reason fits its column (#1186).
 *
 * overseer_dungeon_run.ended_reason is VARCHAR(200) in a UTF-8 table, and the
 * realm database runs in strict mode. A reason past 200 characters is refused
 * with errno 1406 ("Data too long for column 'ended_reason'"), and the whole
 * UPDATE goes with it: state, ended_at and outcome are not written, so the run
 * stays active and the next run on that map collides with it. A stalled run's
 * reason is built from the dungeon module's stop reason plus the refused skip,
 * and it ran past 200 characters on the dev realm.
 *
 * What this file pins:
 *  - FitEndedReasonColumn keeps a reason of 200 characters or fewer unchanged,
 *    and cuts a longer one to at most 200 characters, counted the way the
 *    column counts them (characters, not bytes), keeping the start and marking
 *    the cut, never splitting a multibyte character.
 *  - DUNGEON_RUN_ENDED_REASON_CHARS equals the VARCHAR width in the base SQL.
 *  - Every statement in src/mod_overseer.cpp that writes ended_reason writes
 *    the fitted value: CloseRun, the heartbeat-cold close, and the insert of a
 *    run that never got inside.
 */

#include "overseer_decisions.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::cerr << "FAIL " << what << "\n";
        ++failures;
    }
}

bool ValidUtf8(std::string const& s)
{
    std::size_t i = 0;
    while (i < s.size())
    {
        unsigned char const c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        if (c < 0x80)
            len = 1;
        else if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        else
            return false;
        if (i + len > s.size())
            return false;
        for (std::size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
                return false;
        i += len;
    }
    return true;
}

std::string ReadFile(char const* path)
{
    std::ifstream in(path);
    if (!in)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}
}  // namespace

int main()
{
    using OverseerDecisions::DUNGEON_RUN_ENDED_REASON_CHARS;
    using OverseerDecisions::FitEndedReasonColumn;
    using OverseerDecisions::Utf8CharCount;

    // --- the cut ------------------------------------------------------------
    Check(FitEndedReasonColumn("") == "", "an empty reason is unchanged");
    Check(FitEndedReasonColumn("client_lost") == "client_lost", "a short reason is unchanged");

    std::string const exact(DUNGEON_RUN_ENDED_REASON_CHARS, 'a');
    Check(FitEndedReasonColumn(exact) == exact, "a 200-character reason is unchanged");

    // 200 characters, 400 bytes: fits the column, so nothing is cut.
    std::string wide;
    for (std::size_t i = 0; i < DUNGEON_RUN_ENDED_REASON_CHARS; ++i)
        wide += "\xC3\xA9";
    Check(FitEndedReasonColumn(wide) == wide, "a 200-character multibyte reason is unchanged");

    // The live shape: a stalled run's reason well past the column.
    std::string const live =
        "dungeon clear stopped: the party never became ready after 180s (Waiting on "
        "a member (low mana)).; it refused 'dc skip' 1 inside map 209 with nothing "
        "left it could reach (a member was sitting to eat or drink throughout). and "
        "the module said nothing else";
    Check(live.size() > DUNGEON_RUN_ENDED_REASON_CHARS, "the live-shaped reason is too long");
    std::string const cut = FitEndedReasonColumn(live);
    Check(Utf8CharCount(cut) <= DUNGEON_RUN_ENDED_REASON_CHARS, "a long ASCII reason is cut to the column");
    Check(Utf8CharCount(cut) >= DUNGEON_RUN_ENDED_REASON_CHARS - 8, "the cut keeps nearly the whole column");
    Check(cut.compare(0, 60, live, 0, 60) == 0, "the cut keeps the start of the reason");
    Check(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "...") == 0, "the cut is marked");

    // One character over, ASCII.
    std::string const over(DUNGEON_RUN_ENDED_REASON_CHARS + 1, 'b');
    Check(Utf8CharCount(FitEndedReasonColumn(over)) <= DUNGEON_RUN_ENDED_REASON_CHARS,
          "201 ASCII characters are cut");

    // Multibyte past the column: counted in characters, and no character split.
    std::string mixed;
    for (int i = 0; i < 150; ++i)
        mixed += "x\xE5\xB0\x8F";  // 300 characters, 600 bytes
    std::string const mixedCut = FitEndedReasonColumn(mixed);
    Check(Utf8CharCount(mixedCut) <= DUNGEON_RUN_ENDED_REASON_CHARS, "a long multibyte reason is cut to the column");
    Check(Utf8CharCount(mixedCut) >= DUNGEON_RUN_ENDED_REASON_CHARS - 8, "the multibyte cut keeps nearly the whole column");
    Check(ValidUtf8(mixedCut), "the multibyte cut does not split a character");
    Check(mixedCut.compare(0, 8, mixed, 0, 8) == 0, "the multibyte cut keeps the start");

    std::string fourByte;
    for (int i = 0; i < 250; ++i)
        fourByte += "\xF0\x9F\x90\x89";
    std::string const fourCut = FitEndedReasonColumn(fourByte);
    Check(Utf8CharCount(fourCut) <= DUNGEON_RUN_ENDED_REASON_CHARS && ValidUtf8(fourCut),
          "four-byte characters are cut whole to the column");

    // --- the constant against the schema ------------------------------------
    std::string const sql = ReadFile("data/sql/characters/base/2026_08_30_00_overseer_dungeon_run.sql");
    Check(!sql.empty(), "can read the dungeon run base SQL");
    std::smatch m;
    std::regex const col("`ended_reason`\\s+VARCHAR\\((\\d+)\\)");
    Check(std::regex_search(sql, m, col), "the base SQL declares ended_reason as VARCHAR(n)");
    if (!m.empty())
        Check(std::stoul(m[1].str()) == DUNGEON_RUN_ENDED_REASON_CHARS,
              "DUNGEON_RUN_ENDED_REASON_CHARS equals the column's VARCHAR width");

    // --- every writer goes through the fitted value -------------------------
    std::string const text = ReadFile("src/mod_overseer.cpp");
    Check(!text.empty(), "can read src/mod_overseer.cpp");

    std::string const closeRun = Body(text, "    void CloseRun(uint32 runId, char const* outcome, std::string const& reason)");
    Check(!closeRun.empty(), "can find CloseRun");
    Check(closeRun.find("OverseerDecisions::FitEndedReasonColumn(reason)") != std::string::npos,
          "CloseRun fits the reason to the column");
    Check(closeRun.find("Esc(reason)") == std::string::npos,
          "CloseRun does not write the raw reason");

    std::string const unentered = Body(text, "    void RecordUnenteredRun(");
    Check(!unentered.empty(), "can find RecordUnenteredRun");
    Check(unentered.find("OverseerDecisions::FitEndedReasonColumn(reason)") != std::string::npos,
          "the never-got-inside insert fits the reason to the column");
    Check(unentered.find("Esc(reason)") == std::string::npos,
          "the never-got-inside insert does not write the raw reason");

    // No statement writes a literal reason past the fit: every assignment and
    // insert of ended_reason takes a bound value.
    Check(text.find("ended_reason = '{}'") != std::string::npos, "ended_reason is written as a bound value");
    std::regex const literal("ended_reason = '[^{]");
    Check(!std::regex_search(text, literal), "no statement writes a literal ended_reason");
    Check(text.find("HEARTBEAT_COLD_REASON") != std::string::npos &&
              text.find("OverseerDecisions::FitEndedReasonColumn(HEARTBEAT_COLD_REASON)") != std::string::npos,
          "the heartbeat-cold close writes its reason through the fit");

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "test_dungeon_run_ended_reason_width: all cases passed\n";
    return EXIT_SUCCESS;
}
