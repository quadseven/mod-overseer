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

#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace OverseerDecisions
{

// A failed aim read is not the same thing as a successful read of an empty
// column. Keep the last known council decision through a transient database
// failure; otherwise one failed poll turns a steady aim into 0 and the next
// successful poll looks like a new errand.
std::map<std::string, uint32_t> QuestAimsAfterRead(
    std::map<std::string, uint32_t> const& previous,
    std::map<std::string, uint32_t> const& loaded, bool readSucceeded);

// THE MODULE'S OWN VERSION, and the reason it lives in this header rather than
// in mod_overseer.cpp: this file is the one part of the module that compiles
// on its own, with no AzerothCore include path (see check.decisions.yml, which
// exists to enforce exactly that). So a test, a tool, or the module itself can
// read the version without dragging a core in behind it.
//
// KEPT IN STEP WITH THE `VERSION` FILE AT THE REPOSITORY ROOT, which is the
// human-facing source of truth and what a release is cut from. Two copies of a
// version string is exactly the kind of thing that drifts silently, so the
// decisions workflow compares them and fails if they disagree. Change both, or
// change neither.
//
// WHY 0.x AND NOT 1.0.0. Semver's promise at 1.0 is a stable public interface.
// This module's public interface is its database schema, which the Overseer
// site reads, and its command surface. Both moved this week: overseer_goal and
// overseer_dungeon_run are new, overseer_roster gained the professions columns,
// and a site built against the newer schema returned 503 against an older
// worldserver. Claiming 1.0.0 today would promise a stability the schema
// demonstrably does not have yet. It goes to 1.0.0 when the schema stops
// moving under the site, and that is a real event worth waiting for.
constexpr char VERSION[] = "0.1.0";


// IS A LIVING CHARACTER UNDER A FLOOR IT CANNOT WALK ONTO? (#174)
//
// A wall check can refuse the step that leaves the walkable world, but it says
// nothing about a character already below geometry. The measured failure was
// an entire party alive and moving at z 59-61 while the city surface above it
// was around z 95. Horizontal movement remained possible on the raw terrain
// beneath the city, so neither a death recovery nor a stall could notice.
//
// THE NAVMESH IS THE FALSE-POSITIVE GUARD. A cave, cellar or building may
// legitimately have another surface well above the character. If its current
// position still belongs to the local walkable mesh, it is an interior, not a
// recovery candidate. Conversely, the raw terrain hidden beneath a city WMO
// has no walkable polygon at the character's height.
//
// Airborne travel is deliberately off the land navmesh, so the adapter passes
// those current player states here before any geometry is interpreted.
//
// AND A SCRIPTED FALL IS ONE OF THEM, which cost a dungeon run to learn.
// mod-dungeon-clear drops the party down a shaft as a measured traversal step,
// and one second into that fall a character is in open air: no polygon under
// it by definition, and a surface reading from the lip it just left or the
// cavern roof above. Live on 2026-09-05 in Wailing Caverns, at 14:25:27
// "DropInHole: MoveFall from (-49.5,47.6,-29.0)" and at 14:25:28 this module
// answered (-49.5, 47.6, -39.8) with a surface at 6.6, moved the tank, and at
// 14:25:29 the other four logged "follow-tank: released (DC tank gone)". The
// run lost its tank at the second of its two traversal moments and the
// operator put him back by hand.
//
// The travel half of this module already knew: GroundedStep's comment says
// "Explicit dungeon jump and drop steps do not use GroundedStep." The recovery
// half was never told, and that is the whole of this defect.
//
// A FALLING CHARACTER SHOULD NEVER BE RECOVERED, scripted or not, and that is
// the more general reason to put it here rather than special-casing dungeons.
// Mid-air there is nothing to be right about: the position is changing every
// tick, no polygon under a falling body means nothing, and a genuine fall out
// of the world resolves itself within seconds when the character lands or
// dies - at which point this rule gets a stable reading to judge, and the
// death drive gets the other case. Recovering mid-fall is guesswork against a
// number that will be stale before the teleport lands.
//
// The adapter reads it from Unit::IsFalling (Unit.h:1718), which is true both
// for the client's own MOVEMENTFLAG_FALLING/FALLING_FAR and for a server-side
// fall spline (Unit.cpp:15934-15938) - and the second of those is exactly what
// MoveFall issues, so the scripted drop is covered by the same question.
bool TerrainRecoveryMayInspect(bool alive, bool teleporting, bool inFlight,
                               bool flying, bool falling, bool inWater,
                               bool onTransport, bool onVehicle);

// `surfaceValid` is separate from the number because the core has two invalid
// height sentinels. Invalid data grants no permission to move a character.
// The boundary is inclusive so a declared ten-yard gap means exactly that,
// rather than ten yards plus one floating-point step.
bool BelowTerrainNeedsRecovery(float currentZ, float surfaceAboveZ,
                               bool surfaceValid, bool hasLocalNavmesh,
                               float minimumGap);

// A large measured separation over a reported local polygon. THIS IS A
// DETECTOR AND NOT A LICENCE TO MOVE ANYBODY: what its caller does about a
// true answer depends entirely on whether that polygon is there, and
// TerrainRecoveryStep below owns that. Read its comment before changing this
// one - it carries the live measurements showing that a bridge, an abbey roof
// and a hidden lower terrain plane all produce the same number here, so the
// number cannot be the thing that tells them apart.
bool LargeSurfaceMismatchNeedsRecovery(float currentZ, float surfaceAboveZ,
                                       bool surfaceValid, bool hasLocalNavmesh,
                                       float overrideGap);

// AND WHAT TO DO ABOUT IT, WHICH IS NOT "SEND IT TO THE LEADER'S BIND POINT".
//
// Measured on the dev world 2026-09-05, over 396 minutes of one worldserver:
// 204 recoveries across the five family members, one every 1.9 minutes,
// continuously, for the whole uptime. That is not a recovery. A remedy that
// runs 204 times has fixed nothing 204 times, and the log said so every time
// without anything noticing.
//
// WHAT THE READINGS ACTUALLY WERE. 79 of the 204 landed inside one 6-by-7
// yard patch of the Northshire road at (-9057, -47, z 88.6), all five
// characters, over six hours. The "surface" above them read 113.7 to 118.9 -
// the arch they were walking under. One of the five was recovered at
// (-10504.7, 1035.7, z 60.5) with a surface at z 97.9 ten seconds after the
// module logged "sent to 'vendor' - creature 491 at 39 yards", and two
// minutes later sold ten items to that same vendor: it was standing next to
// the NPC it had been sent to, under the Sentinel Hill tower, on ground it
// demonstrably could walk. Another was recovered at (-8905.6, -158.5, z 81.9)
// with a surface at z 113.1, which is the abbey roof.
//
// SO THE SURFACE READING IS NOT THE FLOOR THIS CHARACTER FELL THROUGH. The
// adapter probes from sixty yards ABOVE the character and searches the same
// distance down, so what it finds is the highest geometry within sixty yards
// OVERHEAD: a bridge deck, an abbey roof, a watchtower floor. A character
// walking under any of those reads as thirty yards below the world.
//
// AND THE NAVMESH GUARD WAS BEING OVERRULED IN EXACTLY THE CASE IT EXISTS FOR.
// LargeSurfaceMismatchNeedsRecovery fires on a large gap even when the
// character's own position has a walkable polygon under it. In the live
// distribution the vertical gaps were 2.3 recoveries per yard below the
// 25-yard override and 31 per yard in the four yards above it - a cliff at the
// threshold, not a distribution - which puts roughly 115 of the 204 in the
// class "Detour said this character is standing on navigable ground and the
// override moved it anyway". That is the false-positive guard being switched
// off by the backstop that was meant to sit behind it.
//
// TWO THINGS FOLLOW, AND THEY ARE THE WHOLE OF THIS DECISION.
//
// FIRST: A LIVE LOCAL POLYGON IS AN ANSWER, NOT A HINT. If Detour finds
// walkable ground at the character's own height, the character is standing on
// walkable ground and whatever is overhead is architecture. Nothing about a
// vertical gap can overturn that, because a bridge, a roof and a hidden lower
// terrain plane all produce the same number. So this no longer moves such a
// character at all. It says so once, loudly, and stops - which is the
// fail-closed direction here, since the action under discussion is displacing
// a character that may be perfectly fine.
//
// SECOND: WHEN THERE IS NO POLYGON, LIFT IT STRAIGHT UP. The adapter already
// knows the height it needs and was printing it in every one of those 204
// lines before discarding it: the surface is at the character's own x and y.
// (x, y, surface + clearance) is the same place, on the ground, still on its
// errand. The bind point instead displaced them 140 to 1,900 yards - the
// measured party spread afterwards was 333 yards, it broke four dungeon
// staging attempts that had already assembled, and it cleared the travel and
// quest aims each time, which is a large silent undo repeated 204 times. It
// also cannot converge: the leader's bind at (-8950, -132) is 140 yards from
// the worst patch, and the median time for a character to walk back into the
// condition after being sent there was FOURTEEN SECONDS.
//
// A LIFT IS FALSIFIABLE, WHICH IS THE REST OF THE VALUE. If it works, the
// condition is false on the next poll. If it does not, the condition is true
// again immediately and this says so, rather than a slow walk back disguising
// a failed remedy as a fresh incident. That is what the attempt count below
// is for: one lift, then a loud give-up. Two actions, then silence, per
// episode. Never 204.
//
// AND NO REMEDY MAY CHANGE THE MAP, which is #188 and is why the fallback that
// used to sit between those two is GONE rather than merely unreachable. The
// whole ladder was caught in one trace on the dev realm 2026-09-05:
//
//   16:46:58  'Grog' below the world at map 1 (1202.6, -707.3, 72.3),
//             surface z 97.7, no local navmesh; LIFTED to z 98.2
//   16:47:03  'Grog' the condition is back, so this is the fallback:
//             sent to the leader's bind point
//   16:47:14  'Grog' STILL below the world at MAP 0 (-8902.6, -162.6, 81.9),
//             surface z 128.0, local navmesh PRESENT
//
// Eleven seconds, one ocean, and the same unresolved reading at the far end.
// Every roster member's bind row is map 0 (-8950, -132), the abbey grounds in
// Elwynn, so a fallback taken on Kalimdor lands the character on the other
// continent: the party ended that minute two in Elwynn, two in the Barrens and
// one offline in Stonetalon, which is not a party and cannot run a dungeon on
// either side of the ocean. The fallback also did not fix the condition it
// escalated for, and could not have, because it moved a bad READING rather
// than a bad position. There is no measured success to weigh against that.
//
// AND IT FED ITSELF. The bind point sits under the abbey, whose roof is the
// highest geometry within the probe's sixty yards, so the destination is one
// of the places most reliably guaranteed to read as "below the world" - which
// is exactly the last line above, with `local navmesh PRESENT` naming it a
// false positive out loud. A remedy whose destination re-triggers the detector
// that chose it is a loop, and the "something keeps putting the family under
// Stormwind" in #188's title was this ladder putting them there.
//
// SO THE REMEDY SET IS CLOSED UNDER "SAME MAP, SAME X, SAME Y". The only
// remedy that moves anything is the lift, and a lift is a change of z alone.
// There is no verdict this can return that a caller could turn into a
// cross-map teleport, which is a stronger guarantee than a rung that merely
// never gets chosen: the type says it, so the next change cannot bring it back
// by accident.
enum class TerrainRemedy
{
    // Leave it where it is. Either nothing is wrong, or nothing this module
    // may safely do about it is left.
    Nothing,

    // Straight up to `liftZ`, at the character's own x and y. Its errand,
    // its aims and its party keep going. THE ONLY REMEDY THAT MOVES ANYTHING,
    // and it moves it in z alone (#188).
    LiftToSurface,

    // Say it once, loudly, and stop trying. A repeated identical condition is
    // a bug in this rule or in the world, and either way silence is worse
    // than one warning a person can go and look at.
    //
    // This is now the END OF THE LADDER as well as the answer to a live
    // polygon. When a lift has not stuck, this module cannot fix the character
    // where it stands, and saying so is the whole remedy: the escalation that
    // used to be here relocated the failure to another continent instead, and
    // the character was still below the world when it arrived (#188).
    GiveUp,
};

// ONE POLL'S WORTH OF WORLD, as the adapter measured it. Grouped rather than
// passed as eight positional arguments because the anchor below made this the
// eighth, and a call site where two floats can be swapped without a compiler
// noticing is a bad place to keep a character's map coordinates.
struct TerrainReading
{
    uint32_t mapId{0};
    float x{0.f};
    float y{0.f};
    float z{0.f};
    float surfaceAboveZ{0.f};
    // `surfaceValid` is separate from the number because the core has two
    // invalid height sentinels. Invalid data grants no permission to move.
    bool surfaceValid{false};
    bool hasLocalNavmesh{false};
};

struct TerrainRecoveryVerdict
{
    TerrainRemedy remedy{TerrainRemedy::Nothing};
    // Where a lift goes. Meaningful only for LiftToSurface, and never a
    // sentinel: it is only ever computed from a surface reading the caller
    // declared valid.
    float liftZ{0.f};
};

// The tunables, in one constant a reader can take in at once, following
// RatchetLimits below. The two gaps are the ones the two predicates above
// already take; the caller passes what it always passed.
struct TerrainRecoveryLimits
{
    float minimumGap{0.f};    // BelowTerrainNeedsRecovery's gap
    float overrideGap{0.f};   // LargeSurfaceMismatchNeedsRecovery's gap
    float liftClearance{0.f}; // how far above the surface a lift lands
    // HOW LONG A CHARACTER HAS TO BE FINE BEFORE THE NEXT OCCURRENCE COUNTS
    // AS A NEW EPISODE. Without this the memory is useless: the condition
    // goes false the instant the character is moved, so a streak that reset
    // on the first clean poll would reset every time and the ladder would
    // never climb past its first rung. The measured walk-back was fourteen
    // seconds and the median interval between one character's recoveries was
    // seven to nine minutes, so this has to be minutes, not seconds. ZERO
    // DISABLES THE MEMORY and makes every occurrence a first one, which is
    // the old unbounded behaviour and is offered only so a caller can say so
    // deliberately rather than by passing a number that looks like a bound.
    time_t forgetSeconds{0};
    // HOW FAR A CHARACTER MAY MOVE AND STILL BE IN THE SAME INCIDENT. It has
    // to be comfortably more than the distance a character covers between one
    // occurrence and the next, or every repetition would look like a fresh
    // first occurrence and the ladder would never bound anything: the measured
    // walk back from the leader's bind point was 140 yards. Since #188 nothing
    // this module does moves a character at all in x or y, so what this reads
    // now is the character's own wandering, and 250 still separates "back in
    // the same hole" from "somewhere else entirely". A map change ends the
    // episode outright and needs no distance. ZERO DISABLES THE DISTANCE TEST
    // and leaves only the map check, which is a defensible choice and has to
    // be written.
    float episodeRadius{0.f};
};

// What one character's terrain recovery remembers between polls. Kept inside
// the adapter's own per-character state, world-thread only and unguarded, like
// RatchetState and the give refusals: losing it on a restart costs one extra
// lift and no correctness.
//
// AN EPISODE IS A PLACE, NOT JUST A STRETCH OF TIME, and that was learned the
// expensive way. The first version of this remembered only how many remedies
// had been applied and when, so "the same condition again" meant nothing more
// than "again". Live on 2026-09-05 a lift on map 1 at 13:48 left a rung
// standing, and at 14:25 on map 43 - a different map, a different incident, 37
// minutes later - that leftover rung chose the fallback instead of the lift.
// The fallback is a bind-point teleport, so it ejected the tank from the
// instance and the run lost it. That fallback is gone (#188), but the anchor
// still earns its place: a ladder is only a fair bound on repetition if the
// thing it is counting really is a repetition, so the episode is anchored
// where it started and abandoned when the character is somewhere else, and
// what it protects now is the LIFT'S turn rather than a teleport's.
struct TerrainRecoveryState
{
    // Remedies applied in the current unbroken episode. This is the bound.
    unsigned attempts{0};
    // The "it is standing on a live polygon under a roof" warning, said once
    // per episode. DELIBERATELY NOT A RUNG ON THE LADDER above: nothing was
    // tried, so nothing should be crossed off. A character warned about
    // walking under an arch that then really does fall through the world a
    // minute later still gets the lift first, rather than being handed the
    // give-up because a warning had used the lift's turn.
    bool saidOnGround{false};
    // WHEN THIS MODULE LAST ACTUALLY DID SOMETHING, and deliberately not when
    // it last saw the condition. A poll that issues nothing must not extend
    // the episode, or the episode never ends anywhere the overhead geometry is
    // permanent. Live on 2026-09-05: inside Wailing Caverns every poll reads a
    // large gap over a live polygon, so refreshing the clock on those quiet
    // polls kept one character's episode alive for 37 minutes across two maps,
    // and a ladder rung left over from a lift on map 1 at 13:48 decided what
    // happened to a fall on map 43 at 14:25. It was the bind point, and it
    // ejected him from the instance. 0 = no memory.
    time_t lastAttempt{0};
    // WHEN THE CONDITION WAS LAST TRUE, which is what the forget window is
    // measured from. Deliberately not `lastAttempt`: the window asks "has this
    // character been FINE for a while", and a module that is out of remedies
    // and has gone quiet is not evidence that anything got better.
    time_t lastHeld{0};
    // WHERE THIS EPISODE STARTED. Anchored on the first poll that holds, and
    // the episode is abandoned when the character turns up on another map or
    // more than `episodeRadius` away.
    bool anchored{false};
    uint32_t mapId{0};
    float x{0.f};
    float y{0.f};
};

// One poll, for one character. Reads the two predicates above for the
// condition and this character's own history for the remedy, and updates that
// history in place.
//
// `hasLocalNavmesh` IS ASKED AT THE CHARACTER'S OWN HEIGHT by the adapter, and
// that is worth knowing when reading the branch it drives: a character that
// really is below the world will find no polygon there, so a false is only
// weak evidence of trouble and can be self-confirming. It is the TRUE answer
// that carries weight, because a polygon found at the character's own feet is
// a positive statement about where those feet are. This function is built the
// way round that fact allows: true is trusted and never overruled, false only
// opens the bounded ladder rather than authorizing a displacement outright.
TerrainRecoveryVerdict TerrainRecoveryStep(TerrainRecoveryState& state,
                                           TerrainReading const& reading,
                                           TerrainRecoveryLimits const& limits,
                                           time_t now);

// A VERTICAL GAP IS A STEP'S BUSINESS, NOT AN ERRAND'S.
//
// mod-overseer#203 bounded the short-step fallback so that follower catch-up
// could not keep handing a mountain-top endpoint to it: a single walking step
// cannot bridge twenty yards of height. The bound was written against the
// ERRAND'S endpoint, and it ran before the navmesh was asked. The night it
// merged, four of the five family members were held in place on ordinary
// overland errands - 233 to 1,629 yards off, aims on the Stormwind gate ramp
// at z 162 and 185 from the Elwynn road at z 100 - because every long walk
// across hills ends more than twenty yards above or below where it starts.
//
// The endpoint's height says something about THIS step only when the endpoint
// IS this step, which is to say when the aim is within one step's reach.
// Further than that, the step being taken lands somewhere else entirely, and
// the ground under it is sampled stride by stride by the adapter's own footing
// check, which already refuses a drop and a climb of its own. So: an aim
// beyond one step may always be stepped toward; an aim within one step may be
// stepped onto only if its height is within the gap a step can bridge.
bool StepMayBridgeGap(float span, float verticalGap, float stepYards,
                      float maxGap);

// A VERTICAL GAP AT SHORT RANGE MEANS "ABOVE IT", NOT "NEAR IT" (#217).
//
// WHAT WAS MEASURED. Every distance this module has ever taken against a PLACE
// - the barrier circle, the arrival check, the staging watchdog's ratchet - is
// a two-dimensional one, so nothing in it could tell "fifty yards away" from
// "fifty yards away and eighty yards up". A dungeon door at the bottom of a
// ravine is therefore approached by converging on the point nearest in TWO
// dimensions, which is the ridge directly above it, and the party stops there.
// Two doors, two nights, and the numbers are horizontal distance from the
// staging point paired with height above it:
//
//   Deadmines, staging point (-11208.2, 1665.34, 24.66). One leader's approach
//   sampled as it happened: 1029/+22.0, 534/+10.2, 205/+30.9, 99/+88.3,
//   52/+83.2, 29/+43.1. He descends to the valley, climbs the hill over the
//   entrance, and stops on top of it.
//
//   Wailing Caverns, staging point (-733.71, -2214.91, 16.8). Four of the
//   family at one moment: 10/+150, 80/+184, 99/+80, 210/+71. And mid-approach
//   on the night the campaign was stood down: 22/+151, 32/+114, 74/+194,
//   282/+117.
//
// THE 10/+150 READING IS WHAT THE TWO-DIMENSIONAL MEASUREMENT IS WORTH. The
// barrier radius is ten yards, so that character counted as standing AT the
// staging point while he was a hundred and fifty yards above it, on the wrong
// side of a cliff with no walkable way down. The phase can advance from a
// ledge, and a party can be declared assembled somewhere it cannot leave.
//
// AND IT IS NOT A REPORTING PROBLEM. Six deaths in six minutes on that
// approach, every one of them a fall, covering all five characters, one of them
// twice fifty-seven seconds apart. Standing a party on a rim is not a neutral
// outcome that wastes a backstop; it is where they die.
//
// THE REFUSAL AT THE LIP IS RIGHT AND THE APPROACH IS WRONG. StepMayBridgeGap
// above already stops a character stepping off the rim toward an aim below it,
// and it should - that step is the fall. But it is asked AT THE LIP, where
// there is nowhere left to go and holding position is the only answer left.
// This is the same question asked EARLY, while there is still route left to go
// around, and while the answer can still be "do not send anybody here".
//
// IT IS LITERALLY THE SAME RULE, WIDENED FROM ONE STRIDE TO THE WHOLE APPROACH.
// StepMayBridgeGap says a step of `stepYards` may bridge `maxGap` of height -
// sixty yards along for twenty yards up, as this module has it - and that is
// the only measured statement anything here owns about how much height walking
// absorbs per yard of ground. So the approach rule is that same gradient
// applied over the distance that REMAINS: a walk with `horizontal` yards left
// to run may absorb `horizontal * stepVerticalYards / stepYards` of height, and
// a gap larger than that is a wall rather than a hillside. At exactly one
// step's reach the two rules return the identical answer, which is what makes
// this an extension of the step bound rather than a second opinion about it.
//
// A GAP NO LARGER THAN ONE STEP'S IS NEVER OVERHEAD, WHATEVER THE RANGE, and
// that floor is the same constant read the other way rather than a fudge. A gap
// StepMayBridgeGap would let a character step across is, by that function's own
// statement, a gap walking crosses; calling it a cliff here would contradict
// the rule this is derived from. Without the floor the gradient degenerates at
// the door, where it should not be asked at all: a character standing two yards
// from the point would be "above" it for being two thirds of a yard off in z.
//
// WHERE THE BOUNDARY ACTUALLY FALLS, on the samples above. Everything walking
// is on one side of it and everything stranded on the other, the nearest pair
// being 205/+30.9 (walkable, and he was still on the valley floor) and
// 210/+71.2 (overhead, and she was up on the high ground with the rest of
// them). No measured sample sits in between.

// The two halves of a gap, kept apart because the whole defect is that they
// were only ever added up into one number that held the first.
struct ApproachGap
{
    // The two-dimensional span - what every check in this module used to be.
    float horizontalYards{0.f};
    // SIGNED, SUBJECT MINUS POINT: positive is above it, negative below. The
    // sign is carried for the sentence an operator reads; every test below is
    // on the magnitude, because a door under a ledge and a door over one are
    // the same defect upside down and a rule that knew only one of them would
    // be half a rule.
    float verticalYards{0.f};
    // False when this poll took no reading at all - the character is on another
    // map, or was not found. Distinguished from a gap of zero for the reason
    // DungeonRunMemberState's negative distance already is: an unmeasured gap
    // must never read as an arrival.
    bool measured{false};
};

enum class ApproachShape : std::uint8_t
{
    // No reading this poll. Never Arrived and never Overhead: it fails to the
    // answer that leaves a caller waiting rather than to either of the two that
    // make it act.
    Unmeasured,
    // Near in all three dimensions, and the only shape that may satisfy a
    // barrier or advance a phase.
    Arrived,
    // Short of the point, and what remains is ground a walk can cover. This is
    // the ordinary answer for almost every yard of almost every approach.
    Closing,
    // Above the point, or below it, by more height than the walking that is
    // left can absorb. Not a distance to close but a route to find, and no
    // amount of stepping toward it will help.
    Overhead,
};

// The three numbers the rule is read against. Two of them are deliberately the
// step bound's own: an approach rule that disagreed with the step it ends in
// would be two rules, and the one that fired last would win by accident.
//
// A CALLER THAT LEAVES THE TWO STEP NUMBERS AT ZERO GETS THE OLD BEHAVIOUR -
// nothing is ever Overhead, and arrival is the flat two-dimensional test. That
// is a deliberate degradation rather than an assertion: a zero here means the
// caller has no step bound to extend, and inventing one on its behalf would be
// this function deciding something it was never told.
struct ApproachLimits
{
    float arrivalYards{0.f};       // near enough, horizontally, to count as there
    float stepYards{0.f};          // one step's reach
    float stepVerticalYards{0.f};  // the height one step may bridge
};

ApproachShape ApproachShapeOf(ApproachGap const& gap, ApproachLimits const& limits);

// HOW FAR AWAY IT REALLY IS - the three-dimensional distance, for the ratchet
// that asks whether an approach is closing and for the line an operator reads.
//
// THIS IS THE OTHER HALF OF THE SAME DEFECT. A ratchet fed the horizontal span
// sees a character who climbs a hundred and fifty yards straight up while
// staying ten yards out as having arrived and stopped, which is exactly what it
// looks like from directly overhead. Fed this, the same reading is a hundred
// and fifty yards out and not improving, which is what it is.
//
// Negative when the gap was not measured, which is the same "no reading"
// convention DungeonRunMemberState::distanceFromStage already carries, and a
// value no ratchet can mistake for progress toward anything.
float ApproachDistance(ApproachGap const& gap);

// The gap in words, for the line that says why a run is being closed. Kept
// apart from the verdict for the reason DungeonRunBarrierBlockers already
// gives: a pure function that also builds strings is a pure function that is
// harder to test twice. "52y out and 83y above it" rather than "52y away",
// because every failure line this replaces named the symptom and not the cause.
std::string ApproachWhere(ApproachGap const& gap);

// A ROUTE MUST END WHERE IT WAS ASKED TO END.
//
// PathGenerator answers an unreachable point with the NEAREST POLYGON it could
// find, and hands that back as a path rather than as a refusal. So a caller
// that treats "a path came back" as "there is a way there" has been told
// something the pathfinder never said. The route check has to ask a second
// question of its own: did the route it was handed actually finish at the
// height that was requested?
//
// Observed on the dev world 2026-09-05: the five family members were walking
// Redridge roads near z 100 while their routed aims came back at z 242, 262
// and 303. Each of those was accepted as a real route, walked up the mountain
// it named, and ended in a fall. The operator watched one of the five die that
// way. A route that misses by a hundred and forty yards of height is not a
// route to the place asked for; it is the mesh's best guess at the nearest
// place it could reach, and walking it is walking up a peak.
//
// THE TOLERANCE IS A DISTANCE, so a negative one is not a stricter rule but a
// nonsense one: nothing is within a negative distance of anything. Reading it
// through an absolute value would quietly turn a sign typo into a rule LOOSER
// than the one written, which is the failure mode this whole predicate exists
// to close. It is refused instead, and a refused tolerance refuses the route,
// so the caller falls through to whatever it already does when a route is not
// trusted rather than proceeding on a number nobody meant.
//
// Plain floats and not a position type, because these two files know nothing
// about the core's geometry classes and must not start now. The adapter
// unpacks the routed endpoint and passes the two heights.
bool TravelEndpointWithinTolerance(float routedEndZ, float requestedZ,
                                   float toleranceYards);

// AND THE WHOLE VERDICT ON A ROUTE, WHICH IS WHY INCOMPLETE IS NOT AN INPUT.
//
// mod-overseer#203 refused every path carrying PATHFIND_INCOMPLETE. That flag
// does not mean what the name suggests. The pinned core sets it in exactly one
// place for the ordinary case (PathGenerator.cpp:604-611): the last polygon of
// the corridor is not the destination's polygon. Two completely different
// things arrive under that one flag.
//
//   TRUNCATED, AND HEADING THE RIGHT WAY. The corridor hit MAX_PATH_LENGTH, so
//   the route was cut off partway and the rest will be found on the next ask
//   from further along. The core leaves the actual end position alone here: it
//   is still the place that was requested.
//
//   MOVED, BECAUSE THE PLACE ASKED FOR COULD NOT BE REACHED. The core
//   substitutes the closest point it could get to and SAYS SO, by calling
//   SetActualEndPosition with it (PathGenerator.cpp:344-352, and again at :676
//   when a climb was refused as too steep).
//
// Refusing both cost the family every long walk. Measured after #210 was
// deployed: three characters were sent 455, 510 and 1,040 yards, got no route
// at all because a route that long is always truncated, fell through to the
// short-step fan, and had every bearing refused by the footing check in city
// geometry. They stood still.
//
// So completeness is the wrong question and the core has already answered the
// right one. ASK WHERE THE PATHFINDER SAYS YOU WILL ACTUALLY END UP, which is
// GetActualEndPosition, and compare its height to the height asked for. A
// truncated route passes, because its actual end IS the destination. A
// substituted route is caught, because its actual end is the mountain shoulder
// the mesh settled for. That is the same pairing the core itself uses in
// PathGenerator::IsInvalidDestinationZ (PathGenerator.cpp:1230-1233), which
// measures GetActualEndPosition against a five-yard bound.
//
// WHAT IS STILL REFUSED OUTRIGHT, because no amount of walking improves it:
//
//   noRouteAtAll  - PATHFIND_NOPATH. There is no route and there was no error
//                   in saying so.
//   endIsOffTheMesh - PATHFIND_FARFROMPOLY_END, which the core sets when the
//                   requested end is more than seven yards from any polygon
//                   (PathGenerator.cpp:301). That is not a route to refine, it
//                   is an aim inside a rock or over a hole, and it stays in the
//                   rejection mask on purpose: it is a fact about the PLACE
//                   ASKED FOR rather than about the height a route reached, so
//                   folding it into the tolerance would lose it.
//   tooFewPoints  - two points or fewer is a straight line the core built by
//                   BuildShortcut, not a route over the mesh.
bool RoutedPathGoesWhereAsked(bool noRouteAtAll, bool endIsOffTheMesh,
                              bool tooFewPoints, float actualEndZ,
                              float requestedZ, float toleranceYards);


// WHAT A REALM SAYS ABOUT ITSELF (mod-overseer#184).
//
// THE PROBLEM, STATED AS THE OPERATOR STATES IT. Three realms run this module:
// a live one, a disposable one, and a small hardcore one. They do not run the
// same build. Today one of them is on an AzerothCore twelve days older than the
// other two, and nothing outside a container log says so. Which realm a reader
// is looking at has, until now, been carried entirely by the hostname the page
// came from - and that distinction is being retired. When it goes, a page with
// no other label is a page that will eventually show the live family's
// positions under a promise that it is not the live family.
//
// So the page has to become the label, and this is the part of that the module
// owns: the realm says who it is and what it is running, into its own database,
// and the site renders what it finds there. A passive reader and a self-
// reporting world, rather than a reader that has to be told out of band.
//
// WHY THE COMPOSITION IS HERE AND NOT IN mod_overseer.cpp. Everything below is
// a decision about strings - which facts go in the report, what a declared
// realm kind is allowed to mean, whether a declared commit describes this
// binary. None of it needs a world, a player or a database, and all of it is
// the kind of thing that is wrong in a way no compile catches. Here it is
// reachable by tests/test_build_report.cpp with no core behind it. What stays
// in mod_overseer.cpp is the part that genuinely cannot move: reading the
// environment, asking GitRevision, and writing the rows.

// The standing of a reported fact. Not decoration: a reader that treated all
// three alike would overstate what is actually known. See the table comment in
// data/sql/characters/base/2026_09_03_00_overseer_build.sql.
constexpr char SOURCE_COMPILED[] = "compiled";  // read out of this binary
constexpr char SOURCE_DECLARED[] = "declared";  // handed in by the deployment
constexpr char SOURCE_DERIVED[]  = "derived";   // this module's own verdict

// The environment variables a deployment may set for this module, named here so
// the writer and its tests cannot disagree about them.
//
// WHY `OVERSEER_` AND NOT THE `AC_` PREFIX THE PINS FILE USES. AzerothCore's
// ConfigMgr derives an environment variable name from every one of its own
// config keys by a camelCase-aware transform and reads whatever matches. An
// `AC_`-prefixed name of our own is at best ignored and at worst collides with
// a key nobody was thinking about, and the failure mode of that collision is
// silent. Our own prefix cannot collide with theirs.
//
// EVERY ONE OF THESE IS OPTIONAL. A deployment that sets none of them still
// gets a report - a thinner one, saying what the binary knows about itself and
// admitting it was told nothing else. That is the honest answer, and it is also
// what every realm will produce on the first start after this ships, because
// the manifests that set these are a separate change on a separate cadence.
constexpr char ENV_REALM[]              = "OVERSEER_REALM";
constexpr char ENV_REALM_KIND[]         = "OVERSEER_REALM_KIND";
constexpr char ENV_PIN_CORE[]           = "OVERSEER_PIN_CORE";
constexpr char ENV_PIN_PLAYERBOTS[]     = "OVERSEER_PIN_PLAYERBOTS";
constexpr char ENV_PIN_OLLAMA_CHAT[]    = "OVERSEER_PIN_OLLAMA_CHAT";
constexpr char ENV_PIN_DUNGEON_CLEAR[]  = "OVERSEER_PIN_DUNGEON_CLEAR";
constexpr char ENV_PIN_AH_BOT[]         = "OVERSEER_PIN_AH_BOT";

// The three answers to "is this the live world". THERE ARE THREE ON PURPOSE,
// and the third is the whole safety argument.
//
// A binary question would force every realm that has not said anything into one
// of the two real answers, and both choices are wrong. Defaulting to
// non-production is the accident this feature exists to prevent: an unlabelled
// live realm would render as safe. Defaulting to production would put a
// production banner over the disposable realm and the canary, so the warning
// would be false two times out of three and would be trained away within a
// week - which is the same failure with a longer fuse.
//
// So a realm that has not been told, or has been told something this module
// does not recognise, reports UNKNOWN, and the site renders unknown as an
// alarm rather than as either answer. A typo in a manifest becomes a visible
// question instead of a confident lie.
constexpr char REALM_PRODUCTION[]     = "production";
constexpr char REALM_NON_PRODUCTION[] = "non-production";
constexpr char REALM_UNKNOWN[]        = "unknown";

// Whether the declared upstream pins actually describe this binary.
constexpr char PINS_MATCH[]   = "match";
constexpr char PINS_STALE[]   = "stale";
constexpr char PINS_UNKNOWN[] = "unknown";

// One line of a realm's report about itself.
struct BuildFact
{
    std::string name;
    std::string value;
    std::string source;
};

// The declared realm kind, reduced to one of the three answers above.
//
// Case and surrounding whitespace are forgiven because a YAML value picks both
// up for free. NOTHING ELSE IS. "prod", "PRODUCTION " and "Production" all
// arrive as production; "prd", "live" and "" all arrive as unknown, which is
// the alarm, not the safe answer. Widening this set is a deliberate act - every
// spelling added here is a spelling that can be typed into a manifest and
// believed.
std::string RealmKind(std::string const& declared);

// The commit AzerothCore prints for itself, pulled out of the sentence
// GitRevision::GetFullVersion() returns:
//
//   "AzerothCore rev. 47960183bb03+ 2026-08-28 21:04:11 +0200 (HEAD branch)
//    (Unix, RelWithDebInfo, Static)"   ->   "47960183bb03"
//
// The trailing `+` means the tree had local modifications at build time, which
// is always true here because the build applies this repo's patches. It is not
// part of the commit and is dropped. Empty if the string is not in that shape,
// which is the honest answer for a core that changes its banner one day.
std::string CoreRevision(std::string const& coreVersion);

// DOES THE DEPLOYMENT'S DECLARATION DESCRIBE THIS BINARY, and this is the check
// that makes the declared rows safe to publish at all.
//
// The upstream commits of mod-playerbots, mod-ollama-chat, mod-dungeon-clear
// and mod-ah-bot-plus are compiled in and then unreachable: there is no symbol
// to ask, so the only way they reach the page is for the deployment to say what
// it built. A declaration can be stale - a manifest that names today's pins in
// front of an image built weeks ago declares the wrong SHAs with total
// confidence, and a page that printed them would be worse than a page that
// printed nothing, because it would look authoritative.
//
// The core commit is the one declared fact that CAN be checked, because the
// core also reports itself. So it is used as the witness for all of them: if
// the declared core commit is not the core actually running, the declaration as
// a whole was written for a different image and every SHA in it is suspect.
//
// Returns PINS_MATCH, PINS_STALE, or PINS_UNKNOWN when either side is missing
// or not in a shape that can be compared. Unknown is not a failure - it is what
// a realm that was told nothing correctly reports.
std::string PinsVerdict(std::string const& coreVersion,
                        std::string const& declaredCoreSha);

// The whole report, in the order a reader would want it.
//
// `coreVersion` is GitRevision::GetFullVersion(). `env` maps the names above to
// their values, with anything the deployment did not set simply absent - so a
// caller reads the environment once and this stays testable.
//
// TWO ROWS ARE ALWAYS PRESENT AND THE REST ARE NOT, which is deliberate.
// `realm_kind` and `pins` are always written, including when the answer is
// "unknown", because their absence and their unknown mean different things to a
// reader and it must be able to tell them apart: no row at all means this realm
// has never reported, and that is a fact about the realm rather than about its
// configuration. Everything else is omitted when it was not declared, because
// an empty string pretending to be a commit is worse than a gap.
std::vector<BuildFact> BuildReport(std::string const& coreVersion,
                                   std::map<std::string, std::string> const& env);


// THE BARRIER PREDICATE, KEPT FREE OF EVERY CORE TYPE ON PURPOSE. Nothing
// here touches Player, Map, or PlayerbotAI - it is fed plain facts the
// caller already gathered, so it can be exercised directly by a unit test
// with no world, no bot, and no database, and so a change to how the facts
// are gathered can never also silently change what BARRIER requires.
//
// ALL THREE CONDITIONS ARE FROM THE EPIC, VERBATIM: "hold until ALL are
// within ~10y, alive, and out of combat." A member already inside is also
// ready: it is ahead of the staging point, not absent from the party. Fails
// closed: an empty roster or any member this poll could not even find (a name
// that resolved to nobody, or a distance never measured because the character
// is on a different map) reads as barrier-not-met, never as vacuously met -
// exactly the "geography is necessary but not sufficient" lesson InDungeonRun
// above already had to learn once.
struct DungeonRunMemberState
{
    std::string name;
    bool seen{false};              // false = not found in the world this poll
    bool alive{false};
    bool inCombat{false};
    float distanceFromStage{-1.f}; // negative = not measured (wrong map, or !seen)
    // AND HOW FAR ABOVE OR BELOW IT (#217). Signed, member minus point, and
    // meaningless unless `distanceFromStage` is a real reading - which is why
    // it is a plain float with no sentinel of its own: the distance beside it
    // already says whether this poll measured anything, and a second way of
    // saying the same thing is a second thing to keep in step.
    //
    // A BARRIER THAT ONLY EVER READ THE LINE ABOVE COULD BE SATISFIED FROM A
    // CLIFFTOP. Measured on Wailing Caverns: ten yards out and a hundred and
    // fifty yards up, which the radius test read as "at the staging point".
    // See ApproachShapeOf, which is what the two fields are now read through.
    float verticalFromStage{0.f};
    // ALREADY THROUGH THE DOOR THIS BARRIER IS WAITING OUTSIDE (#165). Measured
    // live: a run sat `active` and unstaged for forty-three minutes while its
    // barrier line read "Ugga (not seen)" and, on the run before, "Ugga (wrong
    // map)". She was neither. She was inside the instance, having walked
    // through early, and the barrier was waiting for her to arrive at a place
    // she had gone past.
    //
    // An inside member satisfies the barrier because the party is already
    // together at the only boundary that matters. The entry predicate still
    // requires every member to be through before the run can be CLEARING, so
    // accepting this state cannot recreate #126's leader-only transition.
    bool inside{false};
};

// A RADIUS BECAME LIMITS (#217), and the extra numbers are not a tuning knob:
// `arrivalYards` IS the radius this used to take, and the two step numbers
// beside it are what turns "within ten yards" into "within ten yards and on
// the same surface". A caller that leaves them at zero gets the flat radius
// test this always was. See ApproachLimits.
bool DungeonRunBarrierMet(std::vector<DungeonRunMemberState> const& members,
                          ApproachLimits const& limits);

// Why a member is failing BARRIER, for the one log line BARRIER prints
// while it waits. Kept separate from the predicate above so the predicate
// itself stays a plain bool with nothing to format - a pure function that
// also builds strings is a pure function that is harder to test twice.
//
// AND IT NAMES THE CAUSE RATHER THAN THE SYMPTOM. "Grug (80y out and 184y
// above it)" is a sentence an operator can act on; "Grug (80y away)", which is
// what this said for the whole of #217, is one that reads as "nearly there"
// about a character standing on a cliff.
std::string DungeonRunBarrierBlockers(std::vector<DungeonRunMemberState> const& members,
                                      ApproachLimits const& limits);

// CAN THIS PORTAL BE APPROACHED AT ALL, ASKED BEFORE A RUN IS OPENED.
//
// THE RULE IS THE TRAVEL LAYER'S, NOT THIS ONE'S, and writing it down here is
// the point. Every aim this module writes for a PLACE rather than a creature is
// `at:<map>:<x>,<y>,<z>`, and the adapter that resolves one refuses it outright
// when the character's own map is not the map named in the aim - "SAME MAP
// ONLY, and that is a refusal rather than a limitation to fix later. MoveFarTo
// paths through PathGenerator, and there is no navmesh across an ocean". A
// staging point on a map the leader is not standing on is therefore a place no
// errand can ever be taken up for, however correct its coordinates are.
//
// WHY IT NEEDED SAYING NOW. Nothing in the run's own code hard-codes map 0: the
// outside map is carried per portal and every comparison already reads it from
// there. But every portal in the table had outside map 0 and so did the family,
// so the two were equal by accident on every poll that has ever run, and the
// first portal on another continent turns that accident into a run that resets
// an instance, claims an aim nothing accepts, moves nobody, and gives up at the
// staging backstop many minutes later. An accident that has always held is not
// a guard.
//
// A ONE-COMPARISON DECISION IS STILL A DECISION. It is here rather than inline
// in the adapter for the reason the file's own header gives: what the module
// decides is testable without a world, and "the outside map is the leader's
// map" is exactly the kind of invariant that gets quietly relaxed by whoever
// adds boat legs or taxi hops later. When that happens this function grows a
// third answer and its test says what changed; an `if` in the middle of a
// coordinator would just be edited.
enum class DungeonApproach : std::uint8_t
{
    // The leader already stands on the map this portal is approached from, so a
    // staging aim on that map is one the travel layer can accept.
    Walkable,
    // The leader is somewhere else entirely. No aim this run could write would
    // be resolved, so the run must not be opened.
    OffOutsideMap,
};

DungeonApproach DungeonPortalApproach(std::uint32_t leaderMapId,
                                      std::uint32_t portalOutsideMapId);

// ------------------------------------------------- where the party waits --
//
// THE STAGING POINT'S ARITHMETIC, AND THE VERDICT ON WHAT IT PRODUCED.
//
// WHAT THIS IS FOR. The adapter derives the point the party gathers at from two
// areatriggers: the door, and where the way back out lands. It reads both from
// the world, and then does a normalise, a scale and two adds - none of which
// needs a world at all. The world lookups stay in the adapter; the sums are
// here, where a test can run them on the numbers a realm actually holds without
// a realm.
//
// AND THE VERDICT IS THE HALF THAT MATTERS. Measured live: a run aimed its
// leader at `at:1:0,0,0` and walked him at the middle of the map grid for the
// length of its backstop. Nothing had gone wrong with the arithmetic - the
// arithmetic never ran. The coordinator's three staging floats are zero
// initialised, one path through the coordinator reached a staging aim without
// ever asking for them to be filled in, and every consumer downstream happily
// formatted the zeros into an errand because a float that was never set is
// indistinguishable from a float that was set to zero.
//
// So "was this point ever resolved" is made a question with an answer, asked
// where the point is USED rather than only where it is derived. A derivation
// that is checked only at the point of derivation protects exactly the paths
// that call the derivation, which is the set of paths that were never the
// problem.
enum class StagingPointVerdict : std::uint8_t
{
    // A real place on a real map, and the only verdict a staging aim may be
    // built from.
    Usable,
    // The map origin. This is not a judgement about the ground there; it is the
    // observation that three floats holding exactly zero are what "nobody has
    // resolved this yet" looks like, and that no portal in this module's table
    // has an approach corridor passing through the middle of its continent. A
    // derivation that genuinely landed on the origin would be refused too, and
    // that is the right trade: the sentinel reading is worth far more than the
    // point.
    //
    // TESTED IN TWO DIMENSIONS, because every distance this module measures
    // against a staging point is a 2D one (the barrier circle, the arrival
    // check, the watchdog), so an x and y of zero is the sentinel whatever the
    // z beside them says.
    Unresolved,
    // Outside the world grid entirely, or not a number at all. A NaN fails
    // every comparison, so the bounds test below catches an arithmetic accident
    // and an infinity by the same route it catches a coordinate that is simply
    // impossible - and it catches the two sentinels a height query returns when
    // it has nothing (-100000, -200000) without needing to name them.
    OffTheMap,
    // The door and the way-back-out landing point are the same place, so the
    // vector between them names no direction to stand off along. Normalising it
    // would be a divide by something near zero dressed up as a bearing.
    NoApproachAxis,
};

// HOW FAR FROM THE MIDDLE OF A MAP THE WORLD GOES. WoW's terrain grid is 64 x 64
// tiles of 533.33333 yards, so the coordinate space runs +/- 17066.666 about the
// origin on both axes. Named here rather than passed in because it is a fact
// about the coordinate system every one of these points lives in, not a tuning
// knob a caller should get to disagree about.
constexpr float MAP_EDGE_YARDS = 17066.666f;

// The point, and what to think of it. `verdict` is the only field a caller may
// act on first: the three floats are meaningful only when it is `Usable`, and
// are left at zero otherwise so that a caller which ignores the verdict is
// refused by the next check rather than handed a plausible-looking wrong place.
struct StagingPoint
{
    StagingPointVerdict verdict{StagingPointVerdict::Unresolved};
    float x{0.f};
    float y{0.f};
    float z{0.f};
};

// Is this a point a staging aim may be built from? Asked of three floats and
// nothing else, so it can be asked at every place one is used.
StagingPointVerdict StagingPointCheck(float x, float y, float z);
bool StagingPointUsable(float x, float y, float z);

// The refusal, in words, for the `why` an operator reads in the log. Kept apart
// from the verdict for the reason DungeonRunBarrierBlockers already gives: a
// pure function that also builds strings is a pure function that is harder to
// test twice.
std::string StagingPointRefusal(StagingPointVerdict verdict);

// THE DERIVATION ITSELF. `door` is the entry areatrigger's own position;
// `back` is where the exit areatrigger's teleport lands, which is a spot on the
// outside map the game itself picked as standable ground in front of the
// entrance. The vector between them is the approach corridor, measured by the
// people who built the corridor, and the staging point is `standoffYards` back
// down it from the door.
//
// The z returned is the landing point's own, which is real standable ground on
// that map by construction. The adapter may refine it by asking the map for a
// ground height and keeping the answer only if StagingGroundBelievable says so;
// it has no better z to fall back to than this one.
StagingPoint DungeonStagingPoint(float doorX, float doorY,
                                 float backX, float backY, float backZ,
                                 float standoffYards);

// Is a height the map answered with believable for a point one short walk from
// a doorway? A staging point that close to a door is on the same floor as that
// door, so a reading tens of yards away from it is either a different surface -
// the clifftop over a tunnel - or one of the sentinels a height query returns
// when it has nothing. Both are answers to refuse rather than to walk at, and
// refusing them by the same test is deliberate: it needs no separate list of
// sentinel values to keep in step with a core.
bool StagingGroundBelievable(float ground, float doorZ, float toleranceYards);

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
    // only ever falls. `RatchetState::seen` says whether a reading exists;
    // ZERO is a real reading, including when WorldObject::GetDistance2d
    // clamps an arrived subject's distance to zero. The caller settles arrival
    // before it asks this, so a zero distance still needs to be ratcheted.
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
    bool seen{false}; // whether any reading has been taken yet
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
bool RatchetProgressed(float reading, float best, RatchetLimits const& limits,
                       bool seen = true);

// The whole thing: compare, then either restart the clock or say how long it
// has been running. A poll that progressed is never also stalled - it has just
// restarted the clock - so the two verdicts read as the alternatives they are.
RatchetVerdict Ratchet(RatchetState& state, float reading, time_t now,
                       RatchetLimits const& limits);

// The clearing watchdog has two remedies for a run that stopped: skip the
// objective a bounded number of times, then leave the instance. Keep this
// policy free of core types so the dangerous boundary is testable without a
// worldserver. Busy runs, boss progress, and movement are explicit inputs;
// the adapter owns measuring those facts and this function owns only what
// they mean.
enum class DungeonClearStallAction
{
    Nothing,
    Skip,
    Extract,
};

DungeonClearStallAction DungeonClearStallDecision(bool bossProgress,
                                                  bool partyBusy,
                                                  bool movementProgress,
                                                  bool stalled,
                                                  unsigned skips,
                                                  unsigned maximumSkips);

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
// AND ONE OF THE FIVE ANSWERS IS NOT ON THE LADDER AT ALL (#217). Every rung
// below is a remedy for a character that has STOPPED WALKING, and all three of
// them assume that making it walk again is the fix. A character standing on the
// rim above the point it was sent to has not stopped walking - it has arrived
// at the only place the route it was given goes, and nothing on the ladder
// changes that. Worse, two of the rungs restart movement, and the ledge is
// still there: six of the six deaths on the Wailing Caverns approach were
// falls, one character twice inside a minute. So that case is recognised BEFORE
// the ladder and takes it out of use rather than climbing it.
enum class StagingNudge
{
    Nothing,        // walking, staged, or nothing worth reading this poll
    Restrategy,     // re-assert the escort's own strategy set
    Reaim,          // re-issue the aim
    ClearMovement,  // discard the movement generator, as the follow stall does
    GiveUp,         // say once that it cannot be staged, and stop
    // ABOVE THE POINT AND NO LONGER CLOSING. Said once, and it is a diagnosis
    // rather than a correction: this member is not short of the staging point,
    // it is over it, and the way in is a route nothing here can supply. The
    // caller's business is to stop pulling it toward the edge - which means
    // ending the errand, not nudging it - and to say the cause out loud.
    Stranded,
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
    bool stranded{false};   // the above-it-not-near-it line has been said
};

// One member, one poll. `measurable` is false when this poll's distance is not
// a reading ABOUT WALKING - the member is in combat, in the air, dead, or on
// another map - and the clock is then held rather than run, for the reason the
// travel drive already holds its own over a flight: half a taxi route goes the
// wrong way round a mountain, and a fight is a pause rather than a stall.
//
// IT TAKES A GAP RATHER THAN A DISTANCE (#217), AND RATCHETS THE WHOLE OF IT.
// The reading is ApproachDistance, not the horizontal span: a member who climbs
// a hundred and fifty yards while staying ten yards out has not arrived and has
// not stalled, he has gone up, and only the three-dimensional reading says so.
// `approach` is what tells this the difference between a member short of the
// point and a member above it; leave its step numbers at zero and this behaves
// exactly as it did when it took a flat distance.
//
// ARRIVAL IS DECIDED HERE TOO, and that is a move rather than an addition: the
// caller used to make it, with a flat radius, and drop the state of anyone
// inside it. A member on the rim satisfied that test and so was never watched
// at all - the one member most in need of watching was the one exempted. The
// state is reset in place instead, so an arrival is still a fresh ladder.
StagingNudge StagingWatchdog(StagingStallState& state, ApproachGap const& gap,
                             bool measurable, time_t now,
                             RatchetLimits const& limits,
                             ApproachLimits const& approach);
// ------------------------------------------------------------- professions --
//
// THE THIRD TENANT, AND THE SAME REASON AS THE OTHER TWO. The roster declares
// what professions a character should END UP holding
// (overseer_roster.professions), and something has to work out what the next
// step towards that is. That arithmetic needs no world at all: it is a declared
// set, a held set, and the ceiling of two primaries the core enforces - so it
// belongs here, where a test can reach it, rather than three ifs deep inside a
// poll that needs a live Player before it will run.
//
// WHY THIS PARTICULAR DECISION IS WORTH PULLING OUT. Giving up a primary
// profession is the only thing this module does that CANNOT BE UNDONE: it
// destroys every point of the skill and every recipe hanging off it. The
// acceptance criterion most likely to be got wrong is therefore also the one
// that costs the most when it is - re-running the assignment must not unlearn
// and relearn what is already correct - and "already correct" has to be
// something a person can read and a test can pin, not a condition that is only
// true by accident of the order two polls happened to run in.

// One primary profession a character actually holds, and what giving it up
// would cost. The value travels WITH the skill rather than being looked up
// again later, because it is the price: it is the number of points that stop
// existing, and a price fetched at a different moment from the decision it
// prices is how a stale one gets paid.
struct ProfessionHolding
{
    unsigned skill{0};
    unsigned value{0};
};

enum class ProfessionStepKind
{
    // NOTHING TO DO, AND THIS IS THE COMMON ANSWER. A character already
    // holding what the roster asked for gets this on every poll, forever, and
    // is therefore never touched by any of it. That is the idempotence, and it
    // is a property of this enum's first value rather than of a guard
    // somewhere downstream.
    Nothing,

    // Give up `skill`, destroying `cost` points, because every slot is full
    // and the roster wants something this character does not hold.
    GiveUp,

    // Take `skill`: a slot is free and the roster wants it.
    Take,
};

struct ProfessionStep
{
    ProfessionStepKind kind{ProfessionStepKind::Nothing};
    unsigned skill{0};
    unsigned cost{0};  // GiveUp only: the skill value this destroys
};

// The next step from what a character HOLDS towards what the roster SAYS.
//
// THE RULES, IN THE ORDER THEY ARE APPLIED, AND WHY EACH IS THE WAY ROUND IT
// IS.
//
//   1. AN EMPTY `wanted` IS "NO OPINION", NOT "HOLD NOTHING". A character
//      nobody has decided about is not one that may be freely rearranged; the
//      safe reading of an absent decision is to leave it exactly as it is. The
//      column comment in 2026_08_26_00_overseer_roster_professions.sql says
//      the same thing about the same value, and this is where it is enforced.
//
//   2. NOTHING MISSING MEANS NOTHING TO DO, AND IT IS ASKED FIRST. This is the
//      idempotence, and it is deliberately NOT written as `held == wanted`: a
//      character that also holds something the roster has no opinion about is
//      left alone rather than tidied up. Only the MISSING half of the
//      comparison can ever cause anything to happen, so the only way to make
//      this function destroy something is to ask for something it does not
//      have.
//
//   3. A FREE SLOT IS FILLED BEFORE ANYTHING IS DESTROYED. Giving something up
//      is only ever a way of making room, so it cannot be the answer while
//      there is room. This is what stops a character holding one profession
//      and one empty slot losing the profession it has - and it is what makes
//      the whole sequence just-in-time: every GiveUp this returns is followed
//      by a Take into the slot it opened, so nobody is left standing around
//      holding nothing at all.
//
//   4. GATHERING BEFORE CRAFTING while both are still missing. A craft with no
//      supply is a skill that sits at 1/75, which is the failure the whole
//      issue is about, so mining is taken before blacksmithing and skinning
//      before leatherworking. It has to be said explicitly because the ids
//      sort the wrong way round for both of those pairs (164 blacksmithing
//      before 186 mining, 165 leatherworking before 393 skinning), so "take
//      them in id order" would get the supply chain backwards every time.
//
//   5. THE CHEAPEST THING THE ROSTER DOES NOT WANT IS WHAT GOES, and something
//      the roster DOES want is never a candidate however cheap it looks. That
//      second half is not an optimisation. It is what makes a character
//      already holding its assigned pair unreachable by the destructive branch
//      at all, rather than merely unlucky enough not to be picked.
//
// A NOTHING WITH SOMETHING STILL MISSING IS A REAL ANSWER, and the caller can
// tell it from rule 2's Nothing by asking `wanted` again: it means every slot
// is full of skills the roster ALSO wants, so the declared end state does not
// fit in `maxPrimary` and no amount of polling will change that. Nothing is
// destroyed in that case, which is the right direction to fail in for a plan
// nobody can satisfy.
ProfessionStep NextProfessionStep(std::vector<unsigned> const& wanted,
                                  std::vector<ProfessionHolding> const& held,
                                  unsigned maxPrimary);

// ------------------------------------------------------ the give backoff --
//
// "IT DID NOT WORK, AND IT WILL NOT WORK TWO SECONDS FROM NOW EITHER."
//
// The command queue is drained every couple of seconds and the sender is free
// to re-insert a give it has not seen succeed, so a give that cannot succeed
// is not attempted once - it is attempted for as long as the condition lasts.
// Measured on the dev world for mod-overseer#169: 31 rows of one identical
// refusal, the oldest of them hours old, four characters, three items, and
// nothing about any of it changing between one attempt and the next.
//
// The cost is not the work. It is that every one of those attempts is a fresh
// failure with a fresh answer, and everything downstream that reacts to an
// answer reacts again - which is how one blocked hand-over became a line of
// party chat every two seconds for an evening.
//
// SO: REMEMBER THE WALL, AND STOP WALKING INTO IT. A refusal buys a pause. A
// give asked again inside that pause is answered from the memory instead of
// being retried, and the reason is printed only when it is NEW, so the log
// says what is wrong once rather than nine hundred times an hour. The pause is
// short by design: this is a backoff and not a give-up, so the wall is re-
// tested on a cadence a person would use rather than on the queue's.
//
// KEPT FREE OF EVERY CORE TYPE, like everything else in this pair of files, so
// the rule can be exercised with no world, no bot, and no database - which for
// a rule whose whole content is "what happened LAST time, and how long ago" is
// the only way to test it at all.

// One refused give, remembered.
struct GiveRefusal
{
    std::string reason;  // the detail the last attempt returned
    time_t since{0};     // when that attempt was made; 0 = no memory at all
};

// The refusals collected against ONE RECEIVER, keyed by the give that earned
// them. Grouped by receiver rather than kept flat because that is the unit
// that gets forgotten: what these refusals are usually about is how much room
// the receiver has, so the moment anything DOES reach him, every one of them
// is a stale answer to a question whose facts just changed.
using GiveRefusalBook = std::map<std::string, GiveRefusal>;

// Is this give still inside the pause its last refusal bought? Writes the
// remembered reason into `reason` when it is, so the caller can answer the row
// with the same detail it would have produced by trying again.
//
// Sweeps as it goes: any entry older than `forgetSeconds` is erased, because a
// refusal that outlives its reason is its own bug (the lesson
// WithinHandbackGrace in mod_overseer.cpp already carries) and because a book
// nothing ever removes from is a leak with a slow fuse.
bool GiveHeldOff(GiveRefusalBook& book, std::string const& key, time_t now,
                 time_t backoffSeconds, time_t forgetSeconds, std::string& reason);

// Record a refusal against `key`, and answer whether it is worth SAYING: true
// the first time a reason is seen, false for every repeat of it. The clock is
// restarted either way - that is what makes the next few polls cheap - so
// "say it once" and "try it rarely" stay two separate answers.
bool NoteGiveRefusal(GiveRefusalBook& book, std::string const& key,
                     std::string const& reason, time_t now);

// --------------------------------------------- the command queue drain (#230) --
//
// "THE QUEUE IS NOT STALLED. IT IS ATTEMPTING THE SAME TWENTY ROWS FOREVER."
//
// Measured on the dev realm, 2026-09-05 16:37 UTC. Nothing had reached a
// terminal status since 16:11:03, 337 rows were pending and still growing, and
// the drain was running on time the whole while: the twenty oldest pending rows
// were re-touched every two seconds, the same twenty ids, all `kind = 'sell'`,
// all carrying `detail = 'vendor not in range'`. Twenty is COMMANDS_PER_POLL.
// The 322 rows behind them were never read at all, and not one line of log
// mentioned the queue in twenty minutes.
//
// A retry that keeps its place at the head of a FIFO is not a retry, it is a
// lock. The row that holds the head is fixed by `ORDER BY id ASC`, and the
// refusal handed it back with its low id intact, so the queue drained in the
// only order it could: the same twenty rows, over and over, while every command
// asked after them waited on a sale that was never going to happen from where
// those characters were standing. The module tick was perfectly healthy
// throughout, which is exactly why nobody noticed for twenty five minutes.
//
// TWO RULES ARE HERE, AND NEITHER OF THEM IS "RETRY BETTER". The retry itself
// is deleted at the call site rather than tuned: a refusal now leaves through
// `detail` and `result` and the side that asked re-queues a FRESH row at the
// TAIL, which is what mod-overseer#227 chose for the two sibling verbs an hour
// before this was written, and which the queue was already doing on its own
// (measured: 178 pending sales for 21 distinct asks, the same item queued ten
// times over forty five minutes). What is left here is the two things that
// cannot be fixed by deleting four lines.
//
//   A CLAIM EXPIRES. A row moved to `claimed` before it is executed - which is
//   what makes this queue at-most-once for commands that create items and move
//   characters - is invisible to `WHERE status = 'pending'` for the rest of
//   time if the run holding it goes away. Nothing else in the world ends it. It
//   expires to `error` rather than back to `pending`, because handing one back
//   would undo the exact property the claim exists to provide.
//
//   THE QUEUE SAYS WHEN IT IS NOT DRAINING. This defect was invisible in the
//   log and obvious in one database query, which is the wrong way round. A poll
//   that selects rows and executes none of them, or a backlog whose oldest row
//   is not being reached, is a line of log at the moment it starts rather than a
//   thing somebody finds later. Rate limited, because the poll is every two
//   seconds and a complaint repeated nine hundred times an hour is its own
//   outage, and it says so ONCE when it clears so the log has an end as well as
//   a beginning.
//
// KEPT FREE OF EVERY CORE TYPE, like everything else in this pair of files. A
// rule whose whole content is "how many are waiting, how old is the oldest, who
// holds this, and have we said so lately" needs no world, no bot and no
// database to be exercised, which is the only way to pin the boundaries of it
// at all.

// Is a row sitting in a non-terminal status abandoned, so the drain should end
// it rather than leave it where nothing can select it?
//
// Three ways to get this wrong, which is why it is a function and not an
// inequality at the call site. It must not fire on a claim THIS run holds (that
// row is in flight by definition, and ending it would be the drain cancelling
// its own work a moment before it writes the result). It must not fire on a row
// younger than the lease, because a `verifying` row is supposed to sit for
// VERIFY_GRACE_MS while its post-condition is read back. And an empty
// `claimed_by` on a non-pending row is abandoned on anybody's reading: nothing
// that ever held it can still be holding it under a name that is not there.
//
// `heldForSeconds` is an AGE rather than a pair of timestamps, because that is
// how the answer actually arrives: the call site reads it as TIMESTAMPDIFF from
// the database, so both ends of the subtraction are the database's clock and a
// worldserver whose host clock has drifted cannot expire a live claim or keep a
// dead one.
bool ClaimIsAbandoned(std::string const& claimedBy, std::string const& runToken,
                      time_t heldForSeconds, time_t leaseSeconds);

// What one poll of the drain saw, which is everything the voice below judges.
struct CommandQueueSnapshot
{
    unsigned pending{0};   // rows waiting when this poll picked up its work
    unsigned executed{0};  // rows this poll actually attempted
    unsigned held{0};      // rows this poll selected and could NOT attempt
    // Seconds since the oldest waiting row was last touched. Age since it was
    // touched rather than since it was created, because a row that is being
    // worked on is not being starved even if the ask is old.
    time_t oldestPendingAge{0};
};

// The limits the snapshot is judged against.
struct CommandQueueVoiceLimits
{
    // A waiting row untouched for longer than this is not being reached. It has
    // to be well past a full drain of a deep queue: at COMMANDS_PER_POLL rows
    // every COMMAND_POLL_MS, the queue would have to be many hundreds of rows
    // deep for a two minute old row to be honest work rather than starvation.
    time_t stuckSeconds{0};
    // More rows waiting than this is worth saying once, even while they move.
    unsigned deepRows{0};
    // ...and not worth saying again inside this. The poll is every two seconds
    // and the give backoff next door already records what happens to a log when
    // a per-poll condition gets a per-poll line.
    time_t repeatSeconds{0};
};

// What the drain has already said, so it does not say it again every poll.
struct CommandQueueVoiceState
{
    bool complaining{false};  // an unwell verdict is currently standing
    time_t lastSaid{0};       // when anything was last said
};

enum class CommandQueueVoice
{
    Silent,    // nothing worth a line, or it has been said recently enough
    Deep,      // a lot is waiting, and it is moving
    Stuck,     // rows are waiting and are not being reached
    Recovered  // it was one of the two above, and is not any more
};

// Judge one poll, and remember what was said. STUCK OUTRANKS DEEP: a queue can
// be both, and "it is deep" is the reassuring half of that pair, so a stuck
// queue must never be reported as merely a busy one.
//
// The livelock signature is called out WITHOUT waiting for `stuckSeconds`: a
// poll that selected rows and executed none of them is already wrong, whatever
// the ages say, and on 2026-09-05 that was true on the very first poll and
// stayed true for eight hundred more.
CommandQueueVoice CommandQueueSay(CommandQueueVoiceState& state,
                                  CommandQueueSnapshot const& snapshot, time_t now,
                                  CommandQueueVoiceLimits const& limits);

// ------------------------------------------------------------- gear (#145) --
//
// WHAT AN ITEM IS WORTH TO ONE CHARACTER, AND WHETHER IT MAY WEAR IT AT ALL.
//
// THE TWO DEFECTS THIS ANSWERS, BOTH MEASURED ON THE LIVE FAMILY. A level 27
// warrior tank was wearing a CLOTH robe, LEATHER boots and a LEATHER belt
// alongside four pieces of mail, and a mail boot of item level 19 carrying 113
// armour was not preferred over a leather boot of item level 22 carrying 56 -
// twice the mitigation, passed over because the comparison came down to item
// level. Separately, a level 22 priest is carrying leather boots and leather
// gloves she can never put on, four item levels above the cloth she wears.
//
// WHY IT IS HERE AND NOT IN mod_overseer.cpp. Same reason as everything else
// in this pair of files: a decision that needs nothing from the world is a
// decision a test can reach. This one needs it more than most - it is a
// weighting table, and a weighting table nothing can exercise is a weighting
// table nobody will ever dare change (#134, #116).
//
// ---------------------------------------------------------------------------
// THE WEIGHTING, AND WHY IT IS THIS AND NOT SOMETHING ELSE
// ---------------------------------------------------------------------------
//
// EVERY TERM IS IN ARMOUR POINTS. The score is "how many points of armour this
// piece is worth to this character", so the armour term needs no conversion at
// all and every other term is stated as its exchange rate against it. That is
// what makes the numbers below arguable rather than arbitrary: a reader can
// say "two stamina is not worth four armour to a tank" and be talking about
// something real.
//
// THERE IS NO SEPARATE "WRONG ARMOUR CLASS" PENALTY, ON PURPOSE. It would be a
// constant somebody chose. The armour VALUE already is the penalty, in the
// units the tank actually cares about: the cloth robe measured on the tank
// carries 38 armour where mail on the same character's legs carries 168, and
// that gap is a fact about the two items rather than a number invented to
// express a preference. What the score must not do - and what the upstream one
// does - is multiply the whole weight through by item level afterwards, which
// is how a low-armour piece of the wrong class wins on freshness alone.
// (Upstream's calculator has an armour-type penalty written and commented out,
// mod-playerbots StatsWeightCalculator.cpp:631-636, with the helper it would
// have called, NotBestArmorType, still compiled at :724 and reachable from
// nowhere; the multiply-by-item-level is at :120-128, and the armour stat's
// own weight is 0.001 at :266.)
//
// ITEM LEVEL SURVIVES ONLY AS A TIEBREAK, half a point per level. It is a
// proxy for everything not modelled here - a proc, a set bonus, a resistance -
// and a proxy is worth having as a nudge and disastrous as the whole answer.
// Half a point per level cannot move a decision armour or stats have already
// made, and it does break a tie between two otherwise equal pieces in favour
// of the newer one.
//
// PROFICIENCY IS READ PER CHARACTER AND NEVER INFERRED FROM CLASS. The caller
// fills the five booleans below out of the character's own skills, because
// "warrior" does not mean "plate": a warrior learns plate at level 40, the
// tank measured here is 27, and his skill rows are mail, leather, cloth and
// shield with no plate row at all. Deriving it from class and level instead -
// which is what upstream does, mod-playerbots RandomItemMgr.cpp:1069-1078 -
// happens to get that one case right by arithmetic and would get a future
// party member, a heirloom, or a class change wrong. The core's own rule is a
// skill lookup: an item's armour subclass maps to a skill (AzerothCore
// ItemTemplate.h:782-796) and equipping it needs a nonzero value in that skill
// (PlayerStorage.cpp:2344-2366). Worth knowing that the template-only check
// every "can this bot use it" path starts from,
// Player::CanUseItem(ItemTemplate const*), does NOT make that test at all
// (PlayerStorage.cpp:2377-2421) - it checks faction, class mask, race,
// RequiredSkill, RequiredSpell and level, and lets a priest straight through
// on a pair of leather boots.

enum class GearRole
{
    // No opinion, and the score says so rather than pretending otherwise.
    // Every STAT counts one for one - which is the only neutral answer there
    // is, since with no role there is nothing to prefer - while the armour,
    // weapon-damage and item-level terms keep middle values belonging to no
    // role in particular (0.20, 4.0 and the same 0.5 everyone gets). What
    // comes out is a rough ordering, not a judgement, and it is marked
    // unjudged for exactly that reason: an unjudged verdict never drives an
    // automatic swap and never casts a vote. See GearVerdict::judged. An
    // honest refusal, not a guess.
    Unknown,
    Tank,
    Melee,
    Ranged,
    Healer,
    Caster,
};

// One stat line off an item, exactly as ItemTemplate::ItemStat carries it: the
// core's ItemModType id and the value. Plain ints, so this file still includes
// no core header.
struct GearStat
{
    int type{0};
    int value{0};
};

// What THIS character may wear, and what it is for.
struct GearWearer
{
    std::string name;
    int level{0};
    GearRole role{GearRole::Unknown};

    // From the character's own skills. See the proficiency note above for why
    // these are booleans the caller fills rather than a class id this file
    // reasons about.
    bool cloth{false};
    bool leather{false};
    bool mail{false};
    bool plate{false};
    bool shield{false};

    // Does the character hold the weapon skill this particular item needs?
    // Resolved by the caller, for the same reason: a skill lookup, not a
    // property of the class.
    bool weaponProficient{true};

    // Does the item's AllowableClass admit this character? Resolved by the
    // caller against the class mask.
    bool classAllowed{true};
};

// An item, reduced to what the score actually reads.
struct GearItem
{
    std::string name;
    int itemClass{0};  // ITEM_CLASS_WEAPON 2, ITEM_CLASS_ARMOR 4
    int subClass{0};   // armour: cloth 1, leather 2, mail 3, plate 4, shield 6
    int inventoryType{0};
    int itemLevel{0};
    int requiredLevel{0};
    int quality{0};
    int armour{0};
    float dps{0.f};
    std::vector<GearStat> stats;

    // Carries an on-use or proc spell this file does not model. Not a reason
    // to refuse the item; a reason to stop claiming the score is the whole
    // story about it. See GearVerdict::judged.
    bool hasEffect{false};

    // The caller could not resolve a random suffix or property into stats, so
    // some of this item's worth is missing from `stats` and the score is a
    // floor rather than a figure.
    bool unresolvedRandomProperty{false};
};

// HOW MUCH OF THE ITEM THE NUMBER COVERS (#221).
//
// `judged` below is a boolean, and a boolean threw away the one distinction
// that decides most of the family's bags: the difference between a number that
// might be too HIGH and a number that can only be too LOW.
//
// An on-equip effect this file does not price, and a random property whose
// stats the caller could not resolve, can only ever ADD to what an item is
// worth. So the score of such an item is not an unknown - it is a FLOOR, and a
// floor that already beats what the character is wearing settles the question
// without the missing part being read at all. Refusing to act on it, which is
// what the boolean did, is how a level 26 mage ended up carrying a blue robe
// scoring 25.8 while wearing a green one scoring 21.8: the robe's worth is
// partly in an on-equip spell, so the score was declared incomplete and the
// robe stayed in the bag forever.
//
// An unknown ROLE is a different thing altogether and must not be confused
// with it. There the weights are all 1.0 and the ordering is a rough opinion
// rather than a bound in either direction, so nothing may be concluded from it
// in either direction. That is the case the boolean was right about.
//
// Three answers, then, and not two.
enum class GearConfidence
{
    // The score is the whole of what this file can see, and nothing it could
    // not see is missing. A refusal is Exact too: "she cannot wear leather" is
    // certain, and so is the zero an empty slot is worth.
    Exact,
    // The score is a LOWER BOUND. Something unread - an effect, an unresolved
    // random property - can only add to it.
    Floor,
    // Not even a bound. The role is unknown, or the thing is not worn gear at
    // all, so the number orders items roughly and proves nothing.
    Opinion,
};

struct GearVerdict
{
    // May this character put it on at all? False is final: no score, no
    // upgrade, no Need. This is the half that stops a priest needing leather.
    bool wearable{false};

    // In armour points. Meaningless unless `wearable`.
    float score{0.f};

    // Is the score the whole story? False when the role is unknown, when the
    // item's worth is partly in an effect this file does not read, or when a
    // random property could not be resolved. An unjudged verdict never drives
    // an automatic swap or a Need roll - it is said out loud instead.
    //
    // KEPT AS IT WAS, and it is exactly `confidence == GearConfidence::Exact`.
    // The Need vote and the sibling hand-off both read it and both want the
    // strict answer; only the swap needed the finer one.
    bool judged{false};

    // The same answer, told apart. See GearConfidence above.
    GearConfidence confidence{GearConfidence::Opinion};

    // One clause, for the line the caller prints: "mail, 113 armour", or
    // "no leather proficiency".
    std::string why;
};

GearVerdict GearScore(GearItem const& item, GearWearer const& who);

// What a candidate actually has to beat in the slot it would go into.
//
// A TWO-HANDER HAS TO BEAT BOTH HANDS, which is the whole of the Severing Axe
// test (#14): a green two-hander is not an upgrade for a tank holding a shield
// worth 445 armour and a block, however good the axe is on its own. An empty
// slot scores zero, so the first item into one is an upgrade by construction.
float GearIncumbent(float mainHandScore, float offHandScore, bool takesBothHands);

// How much better a candidate has to be before it is worth a swap or a Need.
// One percent plus half a point: the percentage keeps it proportionate at
// every level, and the absolute floor stops two near-zero scores trading
// places forever. Deliberately small - the failure being fixed is a family
// that never swaps anything, not one that swaps too eagerly - and the swap is
// one-way, so it cannot oscillate: once the better item is worn, the one now
// in the bag is the lower score.
bool GearIsUpgrade(GearVerdict const& candidate, float incumbent);

// ------------------------------------------- a swap that settles (#221) --
//
// WHAT WENT WRONG, MEASURED. On the dev realm, one character's hands slot
// filed 124 equip events across eleven hours between the same two pairs of
// gloves - a level 23 rare and a level 29 common, both 122 armour - and twelve
// slots across four of the five characters behaved the same way, 923 equips in
// the hours a flip happened. Nothing was broken in either half. Each half was
// individually correct and individually convergent, and they pointed opposite
// ways:
//
//   - This file scores the rare higher for a tank, because 8 strength and 3
//     stamina beat 6 stamina and 5 spirit at equal armour, and GearIsUpgrade's
//     one-way margin means it will only ever move the character TOWARDS it.
//   - Upstream's own auto-equip multiplies an item's whole stat weight through
//     by its item level and then wants a 1.1x margin, which makes the common
//     item win by 29/23, and it will only ever move the character towards THAT.
//
// Two monotone rules, opposite directions, each on its own timer. Neither can
// oscillate alone; together they cannot do anything else. Both of the swaps
// were "correct" every single time, which is why nothing in either half's logs
// looked wrong, and why it ran for days.
//
// So the settlement is not a better margin. A margin cannot help: whatever it
// is, the other writer has its own. The settlement is that ONE of them decides,
// and that this one NOTICES when something disagrees with it instead of
// arm-wrestling in silence. The first half is a deployment setting. This is the
// second half, and it is here rather than in the adapter because "have I been
// overruled?" is a judgement and belongs where it can be tested.

// Is the candidate better than what is worn, given how much of each score the
// file can actually vouch for? Three answers, because "I cannot tell" is a real
// and common one and reporting it as "no" is what buried the bags.
enum class GearComparison
{
    // Certainly better. Put it on.
    Better,
    // Certainly not better. Leave it, and say nothing - most of what a party
    // carries out of a dungeon is this.
    NotBetter,
    // The numbers do not settle it. Leave it and SAY SO: either the candidate's
    // score is a floor that does not clear the margin, or what is worn is
    // itself only a floor and nothing above it can be proved.
    Undecided,
};

// What a candidate is measured against: a number, and how much of what is worn
// that number covers.
struct GearIncumbentScore
{
    float score{0.f};
    GearConfidence confidence{GearConfidence::Exact};
};

// What is worn in one slot, as something to be measured against. An empty slot
// and an item the character can no longer wear are both worth EXACTLY zero -
// certain, not a guess - which is what makes the first item into an empty slot
// an upgrade by construction.
GearIncumbentScore GearWorn(GearVerdict const& worn);

// A TWO-HANDER HAS TO BEAT BOTH HANDS (#14, the Severing Axe). The scores add,
// and the certainty is the WEAKER of the two: a pair is only exactly known when
// both halves are.
GearIncumbentScore GearIncumbentPair(GearIncumbentScore const& mainHand,
                                     GearIncumbentScore const& offHand);

// The rule itself. `GearIsUpgrade` is the margin it uses and is unchanged, so
// the two never disagree about where the line is - only about what to say when
// the line cannot be located.
GearComparison GearCompare(GearVerdict const& candidate, GearIncumbentScore const& worn);

// ---------------------------------------------- and the drive stands down --
//
// The comparison above converges on its own: it is antisymmetric, so under
// unchanged inputs no pair of items can each be Better than the other, and a
// sweep that swaps reaches a fixed point. `tests/test_gear_converges.cpp`
// asserts both properties rather than asserting the arithmetic that happens to
// give them.
//
// THAT IS NOT ENOUGH ON ITS OWN, because it only proves this file cannot fight
// ITSELF. What actually happened was another writer, and no rule of ours can
// stop one existing - a stray admin `autogear`, a deployment setting that comes
// back on an upstream bump, an upstream path that has not been written yet. So
// the drive also remembers what it put where, and gives up on a slot somebody
// keeps undoing.
//
// The budget is deliberately small. Three attempts is enough to ride out a
// transient - an item briefly unequipped by a durability break, a swap the
// server refused once - and small enough that the 124-equip day becomes three
// equips and one line in the log naming both items. Being WRONG and quiet is
// the failure being fixed; being right and quiet was never the requirement.
constexpr int GEAR_REVERSALS_ALLOWED = 3;

// What the drive remembers about ONE character's ONE slot. Entries, not names:
// a slot is disputed over particular items, and any other candidate is a fresh
// question.
struct GearSlotMemory
{
    // The item entry this drive last put into the slot, and what it took off to
    // do it. Zero for a slot it has never touched.
    unsigned chosen{0};
    unsigned displaced{0};

    // How many times it has since found `displaced` back on and `chosen` in the
    // bags again. Nobody but another writer can do that.
    int reversals{0};
};

// Should the swap happen, and what should be remembered afterwards?
struct GearSwapIntent
{
    // Do it.
    bool swap{false};

    // Do not do it, and say out loud that this slot is being fought over. Set
    // once, on the attempt that exhausts the budget, so the line is said once
    // rather than every five seconds forever.
    bool standDown{false};

    // What to store against this character and slot, whatever the answer.
    GearSlotMemory memory;
};

// `wanted` is the caller's GearCompare answer reduced to a yes: only a
// GearComparison::Better reaches here as true. Everything else is the caller's
// to report and is not this function's business.
GearSwapIntent GearIntend(GearSlotMemory const& memory, unsigned candidateEntry,
                          unsigned wornEntry, bool wanted);

// WHO NEEDS WHEN TWO MEMBERS BOTH WANT THE SAME DROP (#145). The one with the
// lower total equipped score, because what a dungeon run raises is the party's
// floor; on a tie, the larger gain, and on a tie in that, the name, so the
// answer never depends on the order the party happens to be walked in.
// Returns an empty string when nobody wants it.
struct GearContender
{
    std::string name;
    float gain{0.f};       // candidate score minus incumbent; must be > 0 to count
    float totalWorn{0.f};  // everything this character is wearing, scored
};

std::string GearNeedWinner(std::vector<GearContender> const& contenders);

// -------------------------------------------------------------- sell (#18) --
//
// WHAT THE VENDOR SALE DECIDES WITHOUT A WORLD, and why so little of it.
//
// kind='sell' is deliberately the dumbest executor in the queue: it is handed
// ONE item_instance guid and sells that or refuses. It never decides WHAT to
// sell. Whether a green is vendor trash or a listing, whether a stack of herbs
// feeds a profession or a purse, whether the family's twelfth Linen Cloth is
// surplus - every one of those is a disposition rule, and a disposition rule
// has to see the whole family's bags, professions, quests and wants at once,
// which is what the bridge outside the worldserver holds and what this module
// does not. An executor that "helpfully" chose would be a second, blinder copy
// of that rule, disagreeing with the first one from inside a compiled module
// nobody can watch. So the three things below are the whole of what the sale
// decides on its own, and each is a pure function so it can be tested here:
//
//   1. what the command text says (ParseSellSpec)
//   2. which vendor, when more than one is in reach (ChooseSellVendor)
//   3. whether a refusal is worth retrying, and where (SellRefusalRetry)

// `guid:<item_instance.guid>` optionally followed by ` count:<n>`. The guid
// names exactly one stack, the way kind='give' and kind='trade' address one
// (an entry would name a TYPE, and a family that carries two stacks of the
// same thing would have the executor choosing, which is the thing it must
// not do). `count` sells part of the stack; absent, the whole stack goes.
// Anything else - a bare number, `entry:`, a count of 0, a third token - is
// rejected rather than guessed at.
struct SellSpec
{
    bool valid{false};
    uint32_t guid{0};
    uint32_t count{0};  // 0 = the whole stack; never 0 when a count was given
};

SellSpec ParseSellSpec(std::string const& command);

// One vendor the seller can interact with, as the world reports it.
struct SellVendorCandidate
{
    float distance{0.f};       // yards from the seller
    bool refusesSales{false};  // CREATURE_FLAG_EXTRA_NO_SELL_VENDOR
};

// The index of the vendor to sell to, or -1 when none will do. The nearest
// vendor that BUYS wins; one flagged as refusing sales is chosen only when it
// is the only kind in reach, and then only so the refusal can be named as the
// vendor's rather than as "no vendor". Ties go to the lower index, so the
// answer never depends on the order a cell sweep happened to produce.
int ChooseSellVendor(std::vector<SellVendorCandidate> const& candidates);

// WHETHER A REFUSAL IS WORTH ASKING AGAIN, AND WHERE. Three answers, because
// the walls a sale can hit are of three different kinds and a sender treating
// them alike either retries a quest item for ever or gives up on a vendor
// that was merely three yards too far:
//
//   Never      the item itself, or the command, is the problem; the same row
//              gets the same answer from every vendor in the world.
//   Elsewhere  this spot is the problem; the same row may succeed once the
//              seller has walked to a (different) vendor.
//   Later      the seller's state is the problem (dead, in flight, a bag not
//              yet emptied, a loot window open); the same row may succeed
//              here in a moment.
//
// Keyed on the `detail` literal the executor returns, which is the one string
// a row carries that both sides read; an unknown literal is `Later`, because
// a refusal this table has never heard of is more likely a new transient than
// a new permanent, and retrying a permanent costs a row while giving up on a
// transient costs the sale.
enum class SellRetry
{
    Never,
    Elsewhere,
    Later,
};

SellRetry SellRefusalRetry(std::string const& detail);

// The word the result JSON carries for each answer: "never", "elsewhere",
// "later". Here rather than in the executor so the string a test pins is the
// string a row carries.
char const* SellRetryWord(SellRetry retry);

// ---------------------------------------------------------- the bank row --
//
// WHAT A kind='bank' ROW MAY SAY, decided here so the executor in
// mod_overseer.cpp starts from a parsed request rather than from text.
//
// The grammar is three verbs and deliberately no more:
//
//     deposit guid:<item_instance.guid>    bags -> bank
//     withdraw guid:<item_instance.guid>   bank -> bags
//     buy slot                             the next bank bag slot, if affordable
//
// GUID ONLY, NO `entry:` FORM, unlike give and trade. Those keep `entry` for
// "the only one they have"; a bank move is exactly where that convenience
// goes wrong, because the same entry can sit in the bags AND in the bank at
// once (that is what a deposit produces), and a withdraw by entry would then
// have two right answers on opposite sides of the counter. item_instance.guid
// names one row, and it is what the side that decides what to move already
// reads out of the database. The executor never chooses an item; it moves the
// one it is told to, or says why it cannot.
//
// WHY THE PARSE IS A PURE FUNCTION. Every refusal a bank row can produce is
// either "the text was wrong" or "the world said no", and only the second
// needs a world. Keeping the first here means a malformed row is refused with
// the same words on every realm, and that the words are tested rather than
// discovered.
enum class BankVerb
{
    None,      // not a bank request at all; `error` says why
    Deposit,
    Withdraw,
    BuySlot
};

struct BankRequest
{
    BankVerb verb{BankVerb::None};
    uint32_t itemGuid{0};   // for Deposit and Withdraw; 0 for BuySlot
    std::string error;      // the refusal literal when verb is None, else empty
};

// Whitespace-tolerant (leading, trailing, and runs between words), otherwise
// literal: lower-case verbs, `guid:` with digits after it, nothing else on the
// line. A guid of 0 is refused rather than passed on, because 0 is the value
// every "not found" path in the core returns and a row asking for it would
// be answered by whichever item that path found first.
BankRequest ParseBankRequest(std::string const& command);

// WHICH BANKER, when a city square has several in reach.
//
// The candidates are every creature flagged as a banker within interaction
// distance, each already asked whether the character may interact with it
// (alive, friendly, not charmed - the core's own gate). The choice is: any
// interactable one before any that is not, the nearest of those, and on a
// tie the lowest id so the answer does not depend on the order the grid was
// walked in. Returns 0 when nothing qualifies.
struct BankerCandidate
{
    uint32_t id{0};           // the creature's guid counter; never 0 for a real one
    float distance{0.f};
    bool interactable{false};
};

uint32_t NearestBanker(std::vector<BankerCandidate> const& candidates);


// ------------------------------------------- the town trip: repair and buy --
//
// TWO MORE EXECUTORS AT THE SAME COUNTER, and the reason they are one section.
//
// A family that clears a dungeon a hundred times has to come back to town in
// between, and the trip has four errands: sell what it does not want (done -
// kind='sell'), put away what it cannot use yet (done - kind='bank'), REPAIR
// what the run wore out, and BUY the food, drink and reagents the next run
// needs. The last two did not exist. Nothing else in this module spends money
// at all; sell and bank only move things.
//
// The retry classes below are the sell path's three, and they are a SECOND
// enum rather than a rename of SellRetry because renaming that one would edit
// a literal tests/test_sell.cpp pins and a column live rows already carry.
// Two enums with the same three members is a smaller cost than a rename that
// reaches an executor already merged and running.
enum class TownRetry
{
    Never,      // the item, the character's class, or the command is the wall
    Elsewhere,  // this spot is the wall; another NPC may answer differently
    Later,      // the character's own state is the wall; here, in a moment
};

// "never", "elsewhere", "later". Here rather than in the executor so the
// string a test pins is the string a row carries.
char const* TownRetryWord(TownRetry retry);

// ------------------------------------------------------------ repair (#18) --
//
// WHAT A kind='repair' ROW MAY SAY.
//
//     all                                  everything worn and carried
//     item guid:<item_instance.guid>       exactly that one item
//
// BOTH FORMS, and the argument for each. `all` is what a player actually does:
// the repair window has one button for it, and it is one packet where the
// per-item form is eighteen, each with its own chance of arriving after the
// character has wandered out of range. It is also the only form whose whole
// cost is one money delta, which makes the read-back a single subtraction
// rather than a reconciliation.
//
// `item guid:` exists because `all` cannot say WHICH item it failed to pay
// for. Player::DurabilityRepair charges per item and simply returns when the
// purse is short, so a repair-all with 40 silver in hand and 60 silver of
// damage on the gear restores some items and leaves others, silently. When the
// purse is thin the sender wants the tank's weapon repaired and not the
// rogue's spare shirt, and that is a choice about ONE item, addressed by the
// guid the way every other item verb in this module addresses one.
//
// NO GUILD-FUNDS FORM, deliberately. CMSG_REPAIR_ITEM carries a third byte
// meaning "take it out of the guild bank", and Player::DurabilityRepair
// honours it - by returning immediately, having repaired nothing and charged
// nothing, when GetGuildId() == 0. The family has no guild (that is its own
// open issue), so the only thing a guild-funds repair could produce here is a
// row that looks exactly like a successful repair and changed nothing, which
// is the failure mode this whole module exists to stop reporting. The grammar
// therefore has no way to ask for it and the executor always sends 0.
enum class RepairVerb
{
    None,  // not a repair request; `error` says why
    All,
    One,
};

struct RepairRequest
{
    RepairVerb verb{RepairVerb::None};
    uint32_t itemGuid{0};  // for One; 0 for All
    std::string error;     // the refusal literal when verb is None, else empty
};

// Whitespace-tolerant, otherwise literal: lower-case words, `guid:` with
// digits after it, nothing else on the line. A guid of 0 is refused rather
// than passed on, because 0 is exactly what the core's repair path reads as
// "no item named, repair everything" - so a row that meant one item and
// carried a 0 would silently become a repair-all and spend the whole purse.
RepairRequest ParseRepairRequest(std::string const& command);

// WHICH REPAIRER, when a town square has several in reach. The nearest town to
// the family's dungeon has four repair-flagged NPCs within a hundred yards of
// each other, two of them standing about five yards apart.
//
// The candidates are the repair-flagged creatures the character may ALREADY
// interact with, so every one of them is inside INTERACTION_DISTANCE and
// walking to the nearer one saves nothing. What is not the same between them
// is the price: Player::GetReputationPriceDiscount returns a per-creature
// multiplier and the repair cost is multiplied by it. So the rule is CHEAPEST
// FIRST, not nearest first - distance breaks a tie in the discount, and the
// index breaks a tie in both so the answer never depends on the order a cell
// sweep happened to produce. Returns -1 for an empty list.
struct RepairerCandidate
{
    float distance{0.f};  // yards from the character
    float discount{1.f};  // GetReputationPriceDiscount; lower is cheaper
};

int ChooseRepairer(std::vector<RepairerCandidate> const& candidates);

// Keyed on the `detail` literal the executor returns. An unknown literal is
// `Later`, for the same reason the sell table gives: a refusal this table has
// never heard of is more likely a new transient than a new permanent, and
// retrying a permanent costs a row while giving up on a transient costs the
// errand.
TownRetry RepairRefusalRetry(std::string const& detail);

// --------------------------------------------------------------- buy (#18) --
//
// WHAT A kind='buy' ROW MAY SAY.
//
//     entry:<item_template.entry> [count:<n>] [max:<copper>]
//
// `entry:` AND NOT `guid:`, which is the opposite of every other item verb
// here, and the reason is that the item does not exist yet. There is no
// item_instance row to name until the purchase creates one. What a vendor
// sells is a TYPE, the packet carries a type, and so does the row.
//
// `count` is the number of PURCHASES, not the number of items, because that is
// what the packet's count means: Player::BuyItemFromVendorSlot stores
// `pProto->BuyCount * count`. For everything a level-20s party restocks that
// factor is 1 and the two numbers are the same, but the read-back multiplies
// rather than assuming, so a vendor selling arrows two hundred at a time is
// counted correctly instead of read as a hundred and ninety-nine missing.
//
// `max` is a copper ceiling on the WHOLE purchase, and it is the one part of
// this grammar the core's packet has no field for. It is here because the
// read-back proves the purse fell by the right amount only AFTER the money is
// gone, and a mispriced row - a count typed with an extra zero, a vendor whose
// price is not what the planner read - is exactly the thing a bot cannot
// notice and cannot undo. A sale can be undone: the item sits in a buyback
// slot. A purchase cannot; the gold is simply spent. `max` lets the sender say
// what it expected to pay and the executor refuse rather than discover.
// Absent, there is no ceiling.
struct BuyRequest
{
    bool valid{false};
    uint32_t entry{0};
    uint32_t count{1};      // purchases, not items; never 0
    bool capped{false};     // whether `max:` was given
    uint32_t maxCopper{0};  // meaningful only when capped
    std::string error;      // the refusal literal when invalid, else empty
};

// The first word must be `entry:`; `count:` and `max:` may follow in either
// order, each at most once. A count of 0 is refused rather than read as 1,
// because the core silently rewrites a count below 1 to 1 and a row asking for
// nothing should be a malformed row rather than a purchase nobody asked for. A
// `max:` of 0 is allowed and means "only if it is free", which is a real thing
// to ask and is distinguishable from absent by `capped`.
BuyRequest ParseBuyRequest(std::string const& command);

// WHICH VENDOR, when several are in reach and only some of them sell the
// thing. The family's town has eleven vendors inside a hundred and fifty
// yards, and the one that sells water is not the one that sells arrows.
//
// The order is: a vendor that stocks the item and has it in stock beats one
// that stocks it and is sold out, which beats one that does not stock it at
// all. Then the cheaper reputation discount, then the nearer, then the lower
// index. A vendor that does not stock the item is still CHOSEN when no better
// one is in reach, and only so that the refusal can name it - "this vendor
// does not stock 4594" is an aim a sender can correct; "no vendor" is not.
struct BuyVendorCandidate
{
    float distance{0.f};
    float discount{1.f};
    bool stocksItem{false};  // the entry is in this vendor's list at all
    bool inStock{false};     // and there are enough of them right now
};

int ChooseBuyVendor(std::vector<BuyVendorCandidate> const& candidates);

TownRetry BuyRefusalRetry(std::string const& detail);

// ------------------------------------------------------- a death's cause --
//
// WHAT WAS MOVING THIS CHARACTER, AND TOWARD WHAT (#188).
//
// THE GAP THIS CLOSES. `overseer_death` has held the victim's own position,
// health and aim since infra#2912, and that has been enough to say WHERE a
// character died and not once enough to say WHY. Two separate investigations
// have now stopped at the same wall: #231 was filed on a plausible mechanism
// for the falls and then refuted from the sources, because nothing recorded
// what had hold of the character at the time. Over one measured day, 55 of 113
// roster deaths carried no travel target at all and 21 carried no quest aim,
// which is not a gap in the reporting so much as the most informative fact
// anybody has established: something was moving these characters that this
// module had not asked to move them.
//
// AND THE DEATHS ARE FALLS ONTO A KILL PLANE, not terrain. 223 under-world
// deaths since 2026-08-30 all landed between z -642.2 and -500.1, and a
// maximum that tight is a threshold rather than ground. Per day the count runs
// 1, 56, 77, 75, 8, 0, 6, so whatever is dropping them is still happening and
// nobody can yet attribute a single one of those drops to a cause.
//
// TWO ANSWERS, KEPT SEPARATE ON PURPOSE. The core's own movement generator is
// a FACT about the character - what actually had hold of it - and the driver
// below is this module's INTERPRETATION of that fact next to its own aims. The
// row carries both, so a reader who thinks the interpretation is wrong can
// re-derive it from the raw answer instead of having to trust it. That is the
// same reason `killer_type` and `killer_name` are both kept.
//
// EVERYTHING HERE IS SAMPLED, NOT LIVE, and for the reason `health_at_death`
// already is: by the time any death hook fires, the core has already torn the
// state down. Unit::setDeathState stops combat and clears the motion master
// before Player::KillPlayer runs, so a death hook asking "were you in combat"
// or "what was moving you" gets the answer "no" and "nothing" every single
// time. The last sample before the death is the only place those facts still
// exist, and at a five-second cadence against falls that complete in nought to
// five seconds it is the right resolution for exactly this question.
enum class MoveGenerator
{
    Unsampled,  // no snapshot has been taken for this character yet
    Idle,       // IDLE_MOTION_TYPE: nothing had hold of it
    Follow,     // FOLLOW_MOTION_TYPE: it was following its leader
    Point,      // POINT_MOTION_TYPE: a scripted move to a coordinate
    Chase,      // CHASE_MOTION_TYPE: it was pursuing something
    Flee,       // FLEEING / TIMED_FLEEING / CONFUSED: combat put it there too
    Thrown,     // EFFECT_MOTION_TYPE: a spline SOMETHING ELSE put it on
    Other,      // waypoint, flight, home, rotate: named so it is not guessed at
};

// WHAT THIS MODULE THINKS WAS DRIVING IT. Deliberately a small vocabulary: a
// column somebody groups by is only useful if the values are few and mean the
// same thing every time.
enum class DeathDriver
{
    Unknown,       // never sampled. Say so rather than guess.
    Recovery,      // THIS MODULE moved it, recently enough to own the death.
    Errand,        // a move toward something this module aimed it at
    Following,     // following the leader, with no aim of its own
    Fighting,      // chasing or fleeing, which is combat either way
    Thrown,        // a spline it did not choose: a fall, a knockback, a drop
    Idle,          // nothing was moving it, which is itself an answer
    Unattributed,  // something was moving it and this module did not ask
};

char const* DeathDriverName(DeathDriver driver);
char const* MoveGeneratorName(MoveGenerator generator);

// One death's worth of attribution, as the adapter sampled it.
struct DeathAttribution
{
    // False when no snapshot has been taken for this character. Everything
    // else here is then meaningless and the answer is Unknown, which is a
    // better column value than a plausible guess.
    bool sampled{false};
    MoveGenerator movement{MoveGenerator::Unsampled};
    // Seconds since this module last issued a terrain-recovery remedy for this
    // character. NEGATIVE means it never has, which is not the same as zero.
    long recoverySeconds{-1};
    // How recent a remedy has to be for the recovery to own the death. ZERO
    // DISABLES THE ATTRIBUTION and makes a recovery never the answer, which a
    // caller has to write deliberately rather than reach by passing a number
    // that looks like a window.
    long recoveryWindow{0};
    bool hasTravelTarget{false};
    bool hasQuestAim{false};
};

// THE PRECEDENCE IS THE DECISION, so it is written out rather than left to the
// order of a switch:
//
//   1. Never sampled beats everything. Unknown is an honest column value and
//      the reason this function exists is that guessing produced two dead-end
//      investigations.
//   2. A recovery this module issued inside the window beats every other
//      answer, INCLUDING the generator. This module's own remedy is the one
//      cause it is in a position to be certain about, and a recovery that
//      kills a character has to be attributable to the recovery even when the
//      core has already moved on to some other generator. It is also the
//      answer most likely to be inconvenient, which is the reason to put it
//      first rather than last.
//   3. A spline it did not choose (Thrown) beats an aim, because being thrown
//      is what happened to it and the aim is only what it had wanted.
//   4. Combat, then following, then idle: each is a positive statement about
//      what had hold of it.
//   5. Anything else is an Errand if this module had aimed it somewhere, and
//      Unattributed if it had not. THAT LAST VALUE IS THE POINT OF THE WHOLE
//      COLUMN: "something moved this character and it was not us" is the
//      finding both previous investigations needed and neither could make.
DeathDriver NameTheDriver(DeathAttribution const& attribution);

// HOW FAR IT DROPPED, from the last sample to the place it died. Negative
// means unsampled and is deliberately distinguishable from zero: "we do not
// know" and "it did not fall" are different findings, and a report that folds
// them together is how a kill plane goes 223 deaths without an explanation.
// A character that ended HIGHER than it was last seen did not fall, so that
// reads zero rather than a negative distance.
//
// WHAT THIS CANNOT SEE, and #243 is the bill for not having said so here.
// This is a position delta across ONE sample gap, so a drop that both begins
// and ends inside that gap is invisible, and a character that was carried
// upward on the way reads a flat zero however far it fell.
//
// Worse, the distance the core BILLS for is not a position delta at all.
// Player::HandleFall charges m_lastFallZ minus the landing height, and
// upstream's dismount sets m_lastFallZ by hand to the character's own feet
// and the landing height to the ground beneath them, so it charges for the
// terrain under a character that never moved. A zero in this column is not
// evidence that a fall did not kill it. Read it through AccountForFall.
float YardsFallen(bool sampled, float lastZ, float deathZ);

// THE CORE'S OWN FALL ARITHMETIC, so a recorded drop can be CHECKED rather
// than eyeballed. Mirrored from Player::HandleFall in the pinned core, and
// mirrored on purpose: these two files may not include a core header, and a
// number nobody could check is how "fell 0.0 yards" was read as a fall from
// height for a day.
//
//   share of max health = SLOPE * (yards - safe fall) + INTERCEPT
//
// gated at MIN_YARDS, below which the core deals nothing at all at any rate,
// and clamped at one, because the core caps fall damage at max health. That
// cap is why a fall which reaches it kills a 656 HP character and a 1,610 HP
// one alike, and why max health tells you nothing about who dies of one.
constexpr float FALL_DAMAGE_SLOPE = 0.018f;
constexpr float FALL_DAMAGE_INTERCEPT = -0.2426f;
constexpr float FALL_DAMAGE_MIN_YARDS = 13.48f;

// The share of max health a drop of this many yards costs, 0 through 1.
// `rate` is the realm's Rate.Damage.Fall, 1.0 on a stock realm.
float FallDamageShare(float yardsDropped, float safeFallYards = 0.f,
                      float rate = 1.f);

// The shortest drop that kills outright from full health: 69.0 yards on a
// stock realm. A recorded drop under this cannot be the whole story.
float LethalFallYards(float safeFallYards = 0.f, float rate = 1.f);

// WHAT THE RECORDED DROP ACCOUNTS FOR. Takes the column exactly as written,
// where a negative value is the unsampled marker YardsFallen returns, so a
// caller reads the row it has rather than reconstructing the sample.
enum class FallAccount
{
    Unsampled,       // no sample: the column says nothing either way
    NoDrop,          // it ended level with, or above, where it was last seen
    TooShortToHurt,  // a real drop, under the distance the core charges for
    Survivable,      // would have hurt it, could not have killed it from full
    EnoughToKill     // would have killed it from full health outright
};
FallAccount AccountForFall(float recordedYardsFallen, float safeFallYards = 0.f,
                           float rate = 1.f);
char const* FallAccountName(FallAccount account);

// ----------------------------- who a character can actually be sent to (#234) --
//
// THE ERRAND CHOSE ITS NPC BY DISTANCE AND NEVER ASKED WHETHER IT COULD BE
// TRADED WITH, and that one omission is most of a day's failures on the dev
// realm. Measured 2026-09-05: an Alliance family of level 24 to 29 parked
// beside a Horde town, aimed at `vendor`, resolved to the nearest one at 118
// to 258 yards, walked to it through level 40 guards, and could never have
// completed the sale. `Player::GetNPCIfCanInteractWith` (Player.cpp:2113-2163)
// ends with `if (creature->GetReactionTo(this) <= REP_UNFRIENDLY) return
// nullptr`, so an unfriendly vendor refuses a character standing on top of it
// exactly as it refuses one a mile away. Distance was never the question.
//
// WHAT CAME OF IT, all downstream of one comparison: 180 `sell` rows refused
// with `vendor not in range` in an afternoon, the queue livelock those
// refusals fed (#230), six deaths in five minutes to `Horde Guard` and five
// earlier to `Stonetalon Grunt` on the walk there, a graveyard spiral because
// dying in hostile ground resurrects you in hostile ground, and the
// cross-continent splits that spiral escalates into (#241).
//
// NEUTRAL IS NOT A CONSOLATION PRIZE, IT IS THE ANSWER. The gate is
// `> REP_UNFRIENDLY`, not `>= REP_FRIENDLY`, and reading it as "friendly"
// would be a worse bug than the one being fixed: for an Alliance party in
// Kalimdor there is no friendly vendor within reach at all, and the shop that
// serves them is a goblin one that is neutral to everybody. Excluding neutral
// would turn "walks to a vendor that refuses it" into "has no vendor", which
// is not an improvement.

// The fields of one FactionTemplate.dbc row that decide a reaction, copied out
// by the caller so this file needs no core type. Names and order are
// FactionTemplateEntry's own (DBCStructure.h:974-984); `enemyFactions` and
// `friendFactions` are that struct's two fixed arrays of four, as vectors,
// with the trailing zeros the DBC pads them with allowed to be dropped.
struct FactionStance
{
    uint32_t faction{0};
    uint32_t flags{0};          // factionFlags
    uint32_t ourMask{0};
    uint32_t friendlyMask{0};
    uint32_t hostileMask{0};
    std::vector<uint32_t> enemyFactions;
    std::vector<uint32_t> friendFactions;
};

// The core's ReputationRank (SharedDefines.h:155-165), with its numbering, so
// a caller can cast one straight into this and so the "greater than
// unfriendly" comparison below is the same comparison the core makes.
enum class Reaction : int
{
    Hated = 0,
    Hostile = 1,
    Unfriendly = 2,
    Neutral = 3,
    Friendly = 4,
    Honored = 5,
    Revered = 6,
    Exalted = 7,
};

// FactionTemplateEntry::IsHostileTo and ::IsFriendlyTo (DBCStructure.h:987-1016),
// reproduced. Both are asymmetric - the enemy and friend lists belong to
// `subject` and are searched for `other`'s faction - so the argument order is
// part of the meaning and not a detail.
bool FactionStanceHostileTo(FactionStance const& subject, FactionStance const& other);
bool FactionStanceFriendlyTo(FactionStance const& subject, FactionStance const& other);

// Unit::GetFactionReactionTo(FactionTemplateEntry const*, FactionTemplateEntry
// const*) (Unit.cpp:7287-7302), which is where the core lands when neither
// side's faction carries a reputation the player can hold. `npc` first,
// `character` second, because that is the direction the core asks in:
// GetNPCIfCanInteractWith asks the CREATURE how it feels about the player.
Reaction FactionStanceReaction(FactionStance const& npc, FactionStance const& character);

// `GetReactionTo(player) > REP_UNFRIENDLY`, which is the whole of what the
// core's interaction gate tests about faction. One function so no call site
// gets to re-derive the threshold, and so a reader can find the >= vs > in
// one place.
bool MayInteractAt(Reaction reaction);

// One spawn of the wanted role standing on the character's own map. The
// caller has already asked whether this character may interact with it, the
// same way the bank and repair candidate lists arrive already asked.
struct TravelTargetCandidate
{
    uint32_t entry{0};
    float distance{0.f};      // yards from the character, two-dimensional
    bool mayInteract{false};
};

enum class TravelTargetVerdict : uint8_t
{
    Chosen,               // `index` names the spawn to walk to
    NothingOfThatKind,    // no spawn of the role is on this map at all
    NoneWillDealWithUs,   // there are spawns and this character may use none
};

struct TravelTargetChoice
{
    TravelTargetVerdict verdict{TravelTargetVerdict::NothingOfThatKind};
    int index{-1};          // into the candidate list, -1 when nothing was chosen
    int nearestRefused{-1}; // the nearest one it may NOT use, for the log line
    std::size_t considered{0};
    std::size_t refused{0};
};

// THE NEAREST SPAWN THIS CHARACTER CAN ACTUALLY USE, and nothing else about
// it. A spawn it may not interact with is not a worse answer than one it can,
// it is not an answer: the errand cannot end there however well the walk goes.
// So they are excluded rather than ranked below, which is the difference
// between this and ChooseSellVendor's flagged-but-still-chosen vendor - that
// one is chosen only so a refusal can be named as the vendor's, and here
// naming it costs a walk through hostile ground.
//
// THE RIGHT ANSWER IS OFTEN FARTHER AWAY AND THAT IS NOT A REASON TO REJECT
// IT. The usable counter for the family this was written for is about 1,470
// yards from the dungeon door while the unusable one is 510, and infra#3359
// measured both before this existed. Distance only ever breaks a tie among
// spawns that passed the gate; the index breaks a tie in distance, so the
// answer never depends on the order a spawn sweep happened to produce.
TravelTargetChoice ChooseTravelTarget(std::vector<TravelTargetCandidate> const& candidates);

// What to put in the log for a choice, or empty when there is nothing worth
// saying (nothing of the kind is on the map at all, which the caller already
// says, or a clean nearest-spawn choice with nothing refused).
//
// A REFUSAL THAT NAMES ITS CAUSE IS WORTH MORE THAN A HOPEFUL JOURNEY, which
// is the whole argument of this section: today the errand walked, and the
// walking is what killed people. So the two lines this builds are the two
// facts an operator needs and could not previously get - that the nearest
// thing of the right kind is one this character may not use, and which one
// was taken instead.
std::string TravelTargetExplanation(TravelTargetChoice const& choice,
                                    std::vector<TravelTargetCandidate> const& candidates);

}  // namespace OverseerDecisions

#endif  // MOD_OVERSEER_DECISIONS_H
