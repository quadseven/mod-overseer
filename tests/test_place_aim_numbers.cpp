/*
 * A place aim's coordinates are plain decimals, and nothing else (#1345).
 *
 * ParsePlaceAim read each coordinate with std::stof, which accepts far
 * more than a decimal: "nan", "inf", hexadecimal floats and exponents all
 * parse as valid, so an `at:` aim holding such text was treated as a real
 * place and only failed later, as distance comparisons against a NaN or an
 * infinity quietly coming out false. The writer (TravelAimAtPosition) only
 * ever emits fixed decimals with an optional minus, so the reader now
 * checks each field is digits with at most one point (and that minus)
 * before stof sees it, the same discipline as the hand parser MailWalkYards
 * uses for its cap.
 *
 * Compiled against src/overseer_decisions.cpp, run from the repo root.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <string>

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

bool Parses(std::string const& aim)
{
    std::uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    return OverseerDecisions::ParsePlaceAim(aim, map, x, y, z);
}

}  // namespace

int main()
{
    // The canonical aim, negative coordinates and all, still parses.
    std::uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    Check("the staging aim parses",
          OverseerDecisions::ParsePlaceAim("at:1:1807.39,-4407.8,-18.4", map, x, y, z));
    Check("its map", map == 1);
    Check("its x", x > 1807.38f && x < 1807.40f);
    Check("its y", y > -4407.81f && y < -4407.79f);
    Check("its z", z > -18.41f && z < -18.39f);
    Check("whole yards parse", Parses("at:0:100,200,300"));
    Check("a zero coordinate parses", Parses("at:0:0,0,0"));

    // What std::stof used to let through.
    Check("nan is refused", !Parses("at:1:nan,-4407.8,-18.4"));
    Check("inf is refused", !Parses("at:1:inf,-4407.8,-18.4"));
    Check("negative inf is refused", !Parses("at:1:-inf,-4407.8,-18.4"));
    Check("a hex float is refused", !Parses("at:1:0x10,-4407.8,-18.4"));
    Check("an exponent is refused", !Parses("at:1:1e3,-4407.8,-18.4"));
    Check("a leading plus is refused", !Parses("at:1:+5,-4407.8,-18.4"));
    Check("a leading space is refused", !Parses("at:1: 5,-4407.8,-18.4"));
    Check("nan in the last field is refused", !Parses("at:1:1807.39,-4407.8,nan"));
    Check("two points are refused", !Parses("at:1:1.2.3,-4407.8,-18.4"));
    Check("a bare minus is refused", !Parses("at:1:-,-4407.8,-18.4"));
    Check("a bare point is refused", !Parses("at:1:.,-4407.8,-18.4"));

    if (failures)
        std::printf("%d place aim number checks failed\n", failures);
    return failures ? 1 : 0;
}
