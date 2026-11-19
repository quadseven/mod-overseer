/*
 * Which vendor a "vendor not in range" refusal was standing next to
 * (wow-overseer#2220), exercised without a world.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - The sweep keeps the NEAREST vendor it saw, with its entry and name, not
 *     the last one and not only the distance. A refusal that says "6.2 yards"
 *     and nothing else cannot be told apart from a refusal beside a spawn
 *     that should never have been walked to; the entry is what separates
 *     them.
 *   - A tie keeps the one already held, so two spawns at one distance do not
 *     trade places between sweeps.
 *   - A sweep that saw nothing writes nothing: no yards, no entry, no name.
 *     A row with `"nearest_vendor_entry":0` would read as a creature.
 *   - `nearest_vendor_yards` keeps the form live rows already carry.
 *   - The name is a string from the world database and is escaped: a quote
 *     or a backslash in it must not end the string or the object early.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

using OverseerDecisions::KeepNearestVendor;
using OverseerDecisions::NearestVendor;
using OverseerDecisions::NearestVendorJson;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNumber(char const* what, long long got, long long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lld, wanted %lld\n", what, got, want);
    ++failures;
}

void CheckWord(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

// ---- which one is kept -----------------------------------------------------

void ASweepThatSawNothingHoldsNothing()
{
    NearestVendor const nearest;
    Check("no reading yet", nearest.yards < 0.f, true);
    CheckNumber("and no entry", nearest.entry, 0);
    CheckWord("and no name", nearest.name, "");
}

void TheFirstVendorSeenIsKept()
{
    NearestVendor nearest;
    KeepNearestVendor(nearest, 12.5f, 3313, "Far Vendor");
    Check("the distance is kept", nearest.yards == 12.5f, true);
    CheckNumber("with its entry", nearest.entry, 3313);
    CheckWord("and its name", nearest.name, "Far Vendor");
}

void ANearerVendorReplacesTheOneHeld()
{
    NearestVendor nearest;
    KeepNearestVendor(nearest, 12.5f, 3313, "Far Vendor");
    KeepNearestVendor(nearest, 6.25f, 3314, "Near Vendor");
    Check("the nearer distance wins", nearest.yards == 6.25f, true);
    CheckNumber("and brings its entry", nearest.entry, 3314);
    CheckWord("and its name", nearest.name, "Near Vendor");
}

void AFartherVendorDoesNot()
{
    NearestVendor nearest;
    KeepNearestVendor(nearest, 6.25f, 3314, "Near Vendor");
    KeepNearestVendor(nearest, 21.9f, 3313, "Far Vendor");
    Check("the distance stays", nearest.yards == 6.25f, true);
    CheckNumber("the entry stays", nearest.entry, 3314);
    CheckWord("the name stays", nearest.name, "Near Vendor");
}

void ATieKeepsTheOneAlreadyHeld()
{
    NearestVendor nearest;
    KeepNearestVendor(nearest, 8.f, 100, "First");
    KeepNearestVendor(nearest, 8.f, 200, "Second");
    CheckNumber("the first of two at one distance is kept", nearest.entry, 100);
    CheckWord("with its own name", nearest.name, "First");
}

void AVendorAtZeroYardsIsAReading()
{
    NearestVendor nearest;
    KeepNearestVendor(nearest, 4.f, 100, "Far");
    KeepNearestVendor(nearest, 0.f, 200, "Underfoot");
    Check("zero is a distance, not 'nothing seen'", nearest.yards == 0.f, true);
    CheckNumber("and it is the nearest", nearest.entry, 200);
}

void WhatIsNotADistanceIsIgnored()
{
    NearestVendor nearest;
    KeepNearestVendor(nearest, -1.f, 100, "Negative");
    Check("a negative distance is not kept", nearest.yards < 0.f, true);
    CheckNumber("nor its entry", nearest.entry, 0);

    KeepNearestVendor(nearest, std::numeric_limits<float>::quiet_NaN(), 101, "NaN");
    KeepNearestVendor(nearest, std::numeric_limits<float>::infinity(), 102, "Inf");
    Check("nor is a distance that is not a number", nearest.yards < 0.f, true);
    CheckNumber("so nothing is held", nearest.entry, 0);

    KeepNearestVendor(nearest, 9.f, 103, "Real");
    KeepNearestVendor(nearest, std::numeric_limits<float>::quiet_NaN(), 104, "NaN");
    CheckNumber("and a real reading is not displaced by one", nearest.entry, 103);
}

// ---- what the row carries --------------------------------------------------

void NothingSeenWritesNothing()
{
    CheckWord("no vendor in the sweep adds no members",
              NearestVendorJson(NearestVendor{}), "");
}

void TheRowNamesTheNearestVendor()
{
    NearestVendor nearest;
    KeepNearestVendor(nearest, 21.9f, 3313, "Far Vendor");
    KeepNearestVendor(nearest, 6.25f, 3314, "Near Vendor");
    CheckWord("yards, then entry, then name, each led by a comma",
              NearestVendorJson(nearest),
              ",\"nearest_vendor_yards\":6.25"
              ",\"nearest_vendor_entry\":3314"
              ",\"nearest_vendor_name\":\"Near Vendor\"");
}

void TheYardsKeepTheFormRowsAlreadyCarry()
{
    // The executor wrote the float through a stream's default form (six
    // significant digits, no trailing zeros); readers of old and new rows see
    // the same shape.
    NearestVendor whole;
    KeepNearestVendor(whole, 8.f, 1, "A");
    Check("a whole number has no decimal point",
          NearestVendorJson(whole).rfind(",\"nearest_vendor_yards\":8,", 0) == 0, true);

    NearestVendor fine;
    KeepNearestVendor(fine, 21.93417f, 1, "A");
    Check("six significant digits",
          NearestVendorJson(fine).rfind(",\"nearest_vendor_yards\":21.9342,", 0) == 0, true);
}

void TheNameIsEscaped()
{
    NearestVendor quoted;
    KeepNearestVendor(quoted, 5.f, 7, "Bob \"the\" Vendor");
    CheckWord("a quote does not end the string",
              NearestVendorJson(quoted),
              ",\"nearest_vendor_yards\":5"
              ",\"nearest_vendor_entry\":7"
              ",\"nearest_vendor_name\":\"Bob \\\"the\\\" Vendor\"");

    NearestVendor slashed;
    KeepNearestVendor(slashed, 5.f, 7, "back\\slash");
    Check("a backslash is doubled",
          NearestVendorJson(slashed).find("\"nearest_vendor_name\":\"back\\\\slash\"") !=
              std::string::npos,
          true);

    NearestVendor control;
    KeepNearestVendor(control, 5.f, 7, std::string("line\nbreak\ttab"));
    std::string const json = NearestVendorJson(control);
    bool raw = false;
    for (char const c : json)
        if (static_cast<unsigned char>(c) < 0x20)
            raw = true;
    Check("no raw control character reaches the row", raw, false);
}

void ADecorativeSpawnIsNamedAsWhatItIs()
{
    // The case the ticket is about: the name in the row is what lets a reader
    // see that a refusal stood beside a decorative spawn. The entry is made
    // up for the test.
    NearestVendor nearest;
    KeepNearestVendor(nearest, 3.9f, 900001, "[DND] TAR Pedestal");
    Check("the bracketed name is carried whole",
          NearestVendorJson(nearest).find(
              "\"nearest_vendor_name\":\"[DND] TAR Pedestal\"") !=
              std::string::npos,
          true);
    Check("with the entry beside it",
          NearestVendorJson(nearest).find("\"nearest_vendor_entry\":900001") != std::string::npos,
          true);
}

}  // namespace

int main()
{
    ASweepThatSawNothingHoldsNothing();
    TheFirstVendorSeenIsKept();
    ANearerVendorReplacesTheOneHeld();
    AFartherVendorDoesNot();
    ATieKeepsTheOneAlreadyHeld();
    AVendorAtZeroYardsIsAReading();
    WhatIsNotADistanceIsIgnored();

    NothingSeenWritesNothing();
    TheRowNamesTheNearestVendor();
    TheYardsKeepTheFormRowsAlreadyCarry();
    TheNameIsEscaped();
    ADecorativeSpawnIsNamedAsWhatItIs();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a vendor refusal names the vendor it stood next to\n");
    return EXIT_SUCCESS;
}
