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
 * WHAT ELSE IS IN HERE. Anything this module decides that needs nothing from
 * the world belongs in this pair of files, not only the dungeon-run predicates
 * that started it. The ratchet at the bottom is the second tenant: the "has it
 * got anywhere, and if not, give up" rule that four separate drives were each
 * carrying their own copy of.
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

#include <ctime>
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
    // ALREADY THROUGH THE DOOR THIS BARRIER IS WAITING OUTSIDE (#165). Measured
    // live: a run sat `active` and unstaged for forty-three minutes while its
    // barrier line read "Ugga (not seen)" and, on the run before, "Ugga (wrong
    // map)". She was neither. She was inside the instance, having walked
    // through early, and the barrier was waiting for her to arrive at a place
    // she had gone past.
    //
    // THIS IS DELIBERATELY NOT A FOURTH WAY TO SATISFY THE BARRIER, and the
    // predicate below ignores it on purpose. Whether a member who is already
    // through counts as staged, or whether nobody may cross until the barrier
    // opens, is #165's own question and a choice between two defensible
    // answers. Naming the state correctly is a prerequisite for either and is
    // neither: what it buys today is a blockers line that cannot be misread,
    // and a give-up that can say what was actually wrong.
    bool inside{false};
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

// ------------------------------------------------------------- the ratchet --
//
// "HAS IT GOT ANYWHERE, AND IF NOT, FOR HOW LONG?" - a question this module
// asks in five places, and one that was written out five times, with five
// clocks and five constants, before it was written once here.
//
// WHY THE MEASUREMENT MATTERS MORE THAN THE CLOCK. This was learned expensively
// on the travel backstop (#63), and that backstop's own comment is the argument
// for all of them. It exists to catch a character STANDING STILL, and it used
// to approximate that as "taking a while" - which is a different thing, and
// wrong in the one case that matters most. Measured on the dev world: a
// character aimed at the Deadmines portal from Elwynn was released 586 yards
// short, having walked 2347 of 2933 yards at 112 yards a minute, about five
// minutes from arriving. It was released as UNREACHABLE while it was visibly
// reaching it, and the log said so in those words. Nothing was stuck; the
// journey was simply longer than a constant that had only ever been asked about
// trainers in the same city.
//
// So ask the question a backstop is actually for. The best reading only ever
// ratchets one way, so beating it means the subject has done something it has
// not managed before on this errand - which no bot jammed against scenery,
// circling, or standing in a field can keep doing, and which a walking bot does
// on every poll. A target that truly cannot be reached still gets given up on:
// the character closes to whatever range it can manage, stops improving, and
// the clock then runs out undisturbed. The bound is on being stuck, where it
// belongs, rather than on distance or on patience alone.
//
// WHAT IS SHARED, AND WHAT IS DELIBERATELY NOT. Shared: the comparison, the
// mark it is made against, the clock that restarts when the mark is beaten, and
// the verdict when it has not restarted for long enough. Not shared, and left
// at each site: what to DO about a stall. Travel releases the errand, a
// crossing gives up on the door, a stalled follower has its movement generator
// cleared and may be nudged again later, a quest that went nowhere collects a
// strike. Those are four reactions to one fact, and they are the only part that
// was ever really different.
//
// THE READINGS ARE NOT ALL THE SAME EITHER, so that is a parameter below rather
// than an average. One caller measures how near it has got to something it was
// sent to; one counts things that have already happened; two measure how far
// they have moved from a mark dropped where they were last seen going
// somewhere. Three rules, three different answers to the same numbers, and all
// three named.
//
// ONE OF THE FIVE IS NOT HERE, ON PURPOSE. The quest-aim backstop
// (DRIVE_AIM_BACKSTOP_SECONDS) has no measurement to ratchet: its progress is
// "the roster names a different quest", an identity rather than a distance, and
// its clock is additionally carried forward across a travel hand-back by an
// amount only that drive knows. It is a plain deadline and it stays one.
// Handing it a distance it does not have, so that this list could read evenly,
// would be inventing a rule rather than sharing one.

// WHAT A READING MEANS. Named for what the number IS rather than for a
// direction, because the meaning is what differs between the sites and the
// direction follows from it.
enum class RatchetReading
{
    // A DISTANCE TO SOMETHING THE SUBJECT IS TRYING TO REACH. Progress is
    // getting NEARER than it has ever been, by more than `margin`, so the mark
    // only ever falls. A mark of ZERO means no reading has been taken yet and
    // never "already arrived": the caller measures with
    // WorldObject::GetDistance2d, which clamps at zero once the subject is
    // within its own size of the target, and it settles arrival before it asks
    // this.
    DistanceToTarget,

    // A COUNT OF THINGS THAT HAVE ALREADY HAPPENED. Progress is a bigger count
    // than has ever been seen. Zero is a REAL reading here (nothing has
    // happened yet) rather than an unset one, and that is the whole difference
    // from the distance above: the first poll of a crossing nobody has
    // crossed yet is not progress, so the clock its caller started when the
    // phase began is left running rather than restarted.
    CountAchieved,

    // A DISTANCE FROM A MARK THE CALLER MOVES to wherever the subject now is.
    // The subject is not going anywhere in particular and there is nothing to
    // get nearer to; the question is whether it has got anywhere AT ALL since
    // the mark was dropped. Any reading past `margin` counts, and the mark goes
    // back to nothing rather than to the reading, because the next reading is
    // measured from the new mark and starts from zero again.
    DistanceFromLastMark,
};

// One site's whole rule, in one constant a reader can take in at once.
struct RatchetLimits
{
    RatchetReading reading{RatchetReading::DistanceToTarget};
    float margin{0.f};  // how much better a reading has to be before it counts
    // How long without progress is long enough. ZERO MEANS THE CALLER COUNTS,
    // and one caller does: the quest re-pick's patience is three consecutive
    // picks that went nowhere, not a number of minutes, so it asks
    // RatchetProgressed below and keeps its own count rather than growing a
    // timer it never had just to be able to use the whole function.
    time_t patienceSeconds{0};
};

// What one subject's ratchet remembers between polls. Every site that keeps one
// keeps it inside its own per-character state, world-thread only and unguarded,
// and loses it on a restart - which costs one restarted clock and no
// correctness, exactly as each of those states already documented for itself.
struct RatchetState
{
    float best{0.f};  // the best reading so far, in the sense named above
    time_t since{0};  // when `best` was last beaten
};

struct RatchetVerdict
{
    bool progressed{false};  // this reading beat the mark; the clock restarted
    bool stalled{false};     // no progress for longer than `patienceSeconds`
};

// Does this reading count as progress? The comparison half of Ratchet on its
// own, for the site whose patience is counted in tries rather than in seconds
// and which therefore has no clock to keep. Pure in the strongest sense: it
// changes nothing and reads nothing but its arguments.
bool RatchetProgressed(float reading, float best, RatchetLimits const& limits);

// The whole thing: compare, then either restart the clock or say how long it
// has been running. A poll that progressed is never also stalled - it has just
// restarted the clock - so the two verdicts read as the alternatives they are.
RatchetVerdict Ratchet(RatchetState& state, float reading, time_t now,
                       RatchetLimits const& limits);

// -------------------------------------------------- the staging watchdog --
//
// "IT IS FAR AWAY" AND "IT IS NOT COMING" ARE DIFFERENT FACTS, and the
// dungeon-run barrier could only ever say the first one. It prints the gap for
// every member on every poll it holds - "Grog (549y away)" - and a character
// walking in from 549 yards away and a character standing at a herb node 549
// yards away produce the identical line. That is narration. What the operator
// asked for is the second fact and an action to go with it, so that a run that
// cannot start is fixed by the module rather than by somebody watching a
// stream.
//
// THE SECOND FACT IS THE RATCHET ABOVE, ASKED WITH A DISTANCE. Is the gap
// closing? A member that keeps beating its own best distance is walking,
// however far out it still is; one that has not beaten it for long enough is
// stalled, whatever the gap says. That is the whole of the detection and it is
// not a new mechanism - it is the one four other drives already share.
//
// THE ACTION IS A LADDER, NOT A VERDICT, because the three things that can be
// wrong with a staged character are different and they are ordered by how much
// putting them right disturbs it:
//
//   1. SOMETHING IS STEERING IT. A strategy that walks the character somewhere
//      of its own choosing is back on its engine - which is measured, not
//      hypothetical, and is what the escort's own strategy set exists to stop.
//      Re-asserting that set moves nothing and costs nothing, so it is first.
//   2. THE WALK WAS LOST. The aim is still on the roster row and the character
//      is not acting on it. Re-issuing costs one restarted spline.
//   3. THE MOVEMENT GENERATOR IS JITTERING WITHOUT ARRIVING. This is the case
//      KeepRosterFollowing measured (#70) and already answers with
//      MotionMaster::Clear(), and it is last because it throws away whatever
//      the character was in the middle of doing.
//
// AND IT IS BOUNDED, because a self-correcting mechanism that never gives up is
// a loop with a log line in it. After the last rung the character is declared
// unstageable ONCE and this stops touching it; the run's own bounds - the
// travel errand's twenty-minute unreachable backstop, and the operator - own it
// from there. Any poll that shows real progress puts the whole ladder back to
// the bottom, so a character that recovers is watched from scratch rather than
// from the rung its last bad patch reached.
enum class StagingNudge
{
    Nothing,        // walking, staged, or nothing worth reading this poll
    Restrategy,     // re-assert the escort's own strategy set
    Reaim,          // re-issue the aim
    ClearMovement,  // discard the movement generator, as the follow stall does
    GiveUp,         // say once that it cannot be staged, and stop
};

// How many rungs there are below GiveUp. Named so the bound is a number a
// reader can see rather than a switch they have to count.
constexpr unsigned STAGING_NUDGE_STEPS = 3;

// What one member's watchdog remembers between polls. Kept inside the
// coordinator's own run state, world-thread only and unguarded, and lost with
// the run on a restart - which costs one restarted clock and no correctness,
// exactly as every other per-character state in this module already documents.
struct StagingStallState
{
    RatchetState progress;
    unsigned escalated{0};  // how many rungs have been climbed
    bool gaveUp{false};     // the give-up line has been said
};

// One member, one poll. `measurable` is false when this poll's distance is not
// a reading ABOUT WALKING - the member is in combat, in the air, dead, or on
// another map - and the clock is then held rather than run, for the reason the
// travel drive already holds its own over a flight: half a taxi route goes the
// wrong way round a mountain, and a fight is a pause rather than a stall.
StagingNudge StagingWatchdog(StagingStallState& state, float distanceFromStage,
                             bool measurable, time_t now,
                             RatchetLimits const& limits);

}  // namespace OverseerDecisions

#endif  // MOD_OVERSEER_DECISIONS_H
