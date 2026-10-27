/*
 * The travel column is counted in characters, not bytes (wow-overseer#1517).
 *
 * overseer_roster.travel_npc is VARCHAR(32) in a UTF-8 table, so MySQL counts
 * characters. The guard used std::string::size(), which counts bytes, and so
 * refused an NPC name with multibyte letters that the column would have held.
 * These cases pin the count and the fit decision every guard asks.
 */

#include "overseer_decisions.h"

#include <cstdlib>
#include <iostream>
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
}  // namespace

int main()
{
    using OverseerDecisions::TRAVEL_AIM_MAX_CHARS;
    using OverseerDecisions::TravelAimFitsColumn;
    using OverseerDecisions::Utf8CharCount;

    // ASCII: one byte, one character.
    Check(Utf8CharCount("") == 0, "an empty aim is zero characters");
    Check(Utf8CharCount("at:1:-4012.3,-1234.5,-0.0") == 25, "an ASCII aim counts its bytes");

    // Two-, three- and four-byte characters each count once.
    Check(Utf8CharCount("\xC3\xA9") == 1, "a two-byte letter is one character");
    Check(Utf8CharCount("\xE5\xB0\x8F") == 1, "a three-byte letter is one character");
    Check(Utf8CharCount("\xF0\x9F\x90\x89") == 1, "a four-byte character is one character");
    Check(Utf8CharCount("Ren\xC3\xA9") == 4, "a name with one accented letter is four characters");

    // The live shape: a 32-character NPC name with accented letters is more
    // than 32 bytes but fits a 32-character column.
    std::string name;
    for (int i = 0; i < 16; ++i)
        name += "\xC3\xA9";  // 16 characters, 32 bytes
    name += "abcdefghijklmnop";  // 32 characters, 48 bytes
    Check(name.size() > TRAVEL_AIM_MAX_CHARS, "the multibyte name is longer than the column in bytes");
    Check(Utf8CharCount(name) == TRAVEL_AIM_MAX_CHARS, "the multibyte name is exactly the column in characters");
    Check(TravelAimFitsColumn(name), "a 32-character multibyte name fits the column");

    // One character more does not fit, whatever its width.
    Check(!TravelAimFitsColumn(name + "x"), "33 characters do not fit");
    Check(!TravelAimFitsColumn(name + "\xC3\xA9"), "33 characters, the last multibyte, do not fit");

    // ASCII at the edge is unchanged from the byte guard.
    Check(TravelAimFitsColumn(std::string(TRAVEL_AIM_MAX_CHARS, 'a')), "32 ASCII characters fit");
    Check(!TravelAimFitsColumn(std::string(TRAVEL_AIM_MAX_CHARS + 1, 'a')), "33 ASCII characters do not fit");

    // Every aim TravelAimAtPosition gives fits, as it always did.
    std::string const aim = OverseerDecisions::TravelAimAtPosition(1, -4012.35f, -1234.56f, -0.0413553f);
    Check(!aim.empty() && TravelAimFitsColumn(aim), "a formatted position aim fits");

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "test_travel_aim_column_chars: all cases passed\n";
    return EXIT_SUCCESS;
}
