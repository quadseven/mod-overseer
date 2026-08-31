/*
 * mod-overseer's pure decisions, in a file something can actually include.
 *
 * The predicates below were written to be testable and said so in their own
 * comments, which are reproduced verbatim underneath: "KEPT FREE OF EVERY CORE
 * TYPE ON PURPOSE ... it can be exercised directly by a unit test with no
 * world, no bot, and no database". That was true of the code and false of the
 * build. They were private static members of OverseerWorldScript, inside
 * src/mod_overseer.cpp, a translation unit with no header of its own - so
 * there was nothing for a test to include and nothing for it to link against,
 * and the only thing exercising them was a text check asserting that the
 * source of these functions contains no `->`. That check is worth having and
 * it is not a test of what they decide. A seam nothing can reach is a comment
 * about a seam.
 *
 * So they live here instead, as free functions in a namespace, in a pair of
 * files that include NOTHING from AzerothCore or from mod-playerbots: <string>
 * and <vector>, and nothing else, on purpose. That is the property worth
 * protecting, and it is the reason this is a separate file rather than another
 * region of the big one. A test - or a reader - can compile these two on their
 * own, and anything that would drag a core type in here has to fail to build
 * rather than quietly end the arrangement.
 *
 * WHAT DID NOT CHANGE: what any of these functions decides. The bodies are the
 * originals and the comments are the originals; only their indentation moved
 * with them, and `static` became namespace scope so a caller outside this file
 * can name them.
 *
 * AzerothCore's module build globs modules/<module>/src for sources and adds
 * that directory to the include path (CollectSourceFiles and
 * CollectIncludeDirectories, both driven from the core's own
 * modules/CMakeLists.txt), so a second .cpp beside mod_overseer.cpp is
 * compiled and `#include "overseer_decisions.h"` resolves with no build file
 * of this module's own. This module has no CMakeLists.txt and does not need
 * one.
 */

#ifndef MOD_OVERSEER_DECISIONS_H
#define MOD_OVERSEER_DECISIONS_H

#include <string>
#include <vector>

namespace OverseerDecisions
{

// THE BARRIER PREDICATE, KEPT FREE OF EVERY CORE TYPE ON PURPOSE. Nothing
// here touches Player, Map, or PlayerbotAI - it is fed plain facts the
// caller already gathered, so it can be exercised directly by a unit test
// with no world, no bot, and no database, and so a change to how the facts
// are gathered can never also silently change what BARRIER requires.
//
// ALL THREE CONDITIONS ARE FROM THE EPIC, VERBATIM: "hold until ALL are
// within ~10y, alive, and out of combat." Fails closed: an empty roster or
// any member this poll could not even find (a name that resolved to
// nobody, or a distance never measured because the character is on a
// different map) reads as barrier-not-met, never as vacuously met -
// exactly the "geography is necessary but not sufficient" lesson
// InDungeonRun above already had to learn once.
struct DungeonRunMemberState
{
    std::string name;
    bool seen{false};              // false = not found in the world this poll
    bool alive{false};
    bool inCombat{false};
    float distanceFromStage{-1.f}; // negative = not measured (wrong map, or !seen)
};

bool DungeonRunBarrierMet(std::vector<DungeonRunMemberState> const& members,
                          float radiusYards);

// Why a member is failing BARRIER, for the one log line BARRIER prints
// while it waits. Kept separate from the predicate above so the predicate
// itself stays a plain bool with nothing to format - a pure function that
// also builds strings is a pure function that is harder to test twice.
std::string DungeonRunBarrierBlockers(std::vector<DungeonRunMemberState> const& members,
                                      float radiusYards);

// THE CROSSING PREDICATES, KEPT FREE OF EVERY CORE TYPE FOR THE SAME REASON
// THE BARRIER ONE IS. Nothing below touches Player, Map or PlayerbotAI, so
// "when may the party be knocked through" can be exercised by a unit test
// with no world, and a change to how the facts are gathered can never
// silently change what a crossing requires.
//
// ONE SHAPE FOR BOTH DIRECTIONS. `through` means "on the far side of this
// door", which for ENTER is the instance map and for EXIT is the map
// outside it. ENTER and EXIT differ in which trigger and which far side,
// and in nothing else, so they share these predicates rather than owning a
// copy each - a party that can get in and cannot get out is a worse failure
// than one that never went in, and two copies is how the second one rots.
//
// WHY THIS IS NOT DungeonRunMemberState WITH A DIFFERENT CENTRE. A member
// that is ALREADY THROUGH is on another map, which to the barrier predicate
// reads as "wrong map" and therefore as not-met - the one state a crossing
// most needs to distinguish would have been indistinguishable from failure.
// Being through is a third answer, not a bad distance, so it is a field of
// its own.
struct DungeonRunEntryState
{
    std::string name;
    bool seen{false};             // false = not found in the world this poll
    bool alive{false};
    bool inCombat{false};
    bool through{false};          // already on the far side of the door
    float distanceFromDoor{-1.f}; // negative = not measured (through, wrong map, or !seen)
};

// Is every member either already through, or standing on the doorstep alive
// and out of combat? Fails closed on an empty roster and on any member this
// poll could not find, exactly as the barrier predicate does and for the
// same reason: a knock for a party that is not all there is the tank
// entering alone with extra steps.
bool DungeonRunEntryReady(std::vector<DungeonRunEntryState> const& members,
                          float doorstepYards);

// Is the crossing finished? Separate from the readiness predicate above
// because "everybody is through" and "everybody may be knocked" are
// different questions with different answers on every poll in between, and
// a single function answering both would have to be asked which it meant.
bool DungeonRunAllThrough(std::vector<DungeonRunEntryState> const& members);

// Why a member is not through yet, for the one line ENTER prints while it
// waits. Kept out of the predicates for the reason DungeonRunBarrierBlockers
// already gives: a pure function that also builds strings is a pure function
// that is harder to test twice.
std::string DungeonRunEntryBlockers(std::vector<DungeonRunEntryState> const& members,
                                    float doorstepYards);

}  // namespace OverseerDecisions

#endif  // MOD_OVERSEER_DECISIONS_H
