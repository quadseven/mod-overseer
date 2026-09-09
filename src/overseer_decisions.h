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
//
// THE `falling` THIS NOW RECEIVES IS MEASURED, NOT THE FLAG (#323).
// Everything above is still exactly right about a fall that is a fall; it is
// wrong about a flag that has stopped describing one. MeasuredToBeFalling
// below is what the adapter passes here, and its comment carries the
// measurement. This declaration is unchanged so the eight arguments still line
// up one-for-one with FallGuardStandDownMask's, which is what stops the two
// drifting.
bool TerrainRecoveryMayInspect(bool alive, bool teleporting, bool inFlight,
                               bool flying, bool falling, bool inWater,
                               bool onTransport, bool onVehicle);

// ------------------------------ a fall that is never going to resolve (#323) --
//
// THE STAND-DOWN ABOVE HAS NO CLOCK ON IT, AND THAT IS WHAT KILLED THEM.
//
// The argument for standing down mid-fall is written out above and it is a
// good one: a scripted drop down a Wailing Caverns shaft is a fall, the
// recovery yanked a tank out of one, and the run lost its tank. Nothing here
// disagrees with any of that. The defect is the OTHER half of the same
// sentence - "a genuine fall out of the world resolves itself within seconds
// when the character lands or dies". It does resolve. It resolves by dying,
// and the drive that could have prevented that is the one waiting for it.
//
// MEASURED ON `overseer_death`, 2026-09-06 to 2026-09-08. Of 146 self-killed
// deaths with a sampled stand-down mask, 50 were on the void plane described
// at VOID_PLANE_Z below. FORTY-EIGHT OF THOSE FIFTY carried FALL_GUARD_FALLING
// at the last poll before the death, which is to say that on the last poll
// this module took of a character it was about to lose, TerrainRecoveryMayInspect
// returned false and the recovery drive did not look at it at all. The two
// that did not carry it are the whole of the population this drive was awake
// for.
//
// AND THE FLAG IS THE ONE #291 ALREADY PROVED CANNOT BE TRUSTED. See
// FallBaselineMayInspect: EffectMovementGenerator::Finalize opens with
// `if (!unit->IsCreature()) return;`, so a player's MOVEMENTFLAG_FALLING is
// never cleared server-side, and this roster sends no client packets to clear
// it either. #291 measured characters standing still with the flag on and a
// descent of 1.63 yards. It took the flag away from the FALL BASELINE and
// deliberately left it deciding here, on the argument that "the recovery may
// reasonably decline to move a character whose flags say falling, because
// moving one is expensive and being wrong about it is worse". Being wrong
// about it turned out to cost 48 characters, so the argument survives and its
// conclusion does not.
//
// SO THE STAND-DOWN KEEPS ITS REASON AND ASKS THE MEASUREMENT INSTEAD.
//
// This needs no new instrument, because #291 already built the right one and
// only wired it to the other drive. FallBaselineStep takes two of this
// character's own positions a second apart and declines to rebase when it is
// losing height faster than FallBaselineLimits allows a body on its feet to.
// That number is already recorded on every death row as
// FALL_GUARD_DESCENDING. It is the answer to the question the `falling`
// argument is really asking, and it is the answer the flag was standing in
// for.
//
// IT SEPARATES THE TWO CASES IN ONE POLL, which is the whole reason to prefer
// it to any clock:
//
//   - the scripted shaft drop the stand-down exists for is a real descent, so
//     it is measured as one and is still stood down for, every poll of it. The
//     log quoted above is one second into that drop and 10.8 yards down, which
//     is free fall and nowhere near the limit;
//   - a character standing still under the terrain with the flag stuck on was
//     measured descending 1.63 and 1.88 yards per second (#281), which is a
//     walk. The drive looks, and that is the case this exists to reach.
//
// WHAT IT STILL CANNOT DO, said plainly rather than left to be discovered. A
// character in genuine continuous free fall out of the world is measured as
// falling and is still stood down for, all the way to the plane. That is not a
// regression and it is not a gap this could close: the adapter's surface probe
// reaches sixty yards, a free fall crosses that in under three seconds, and
// below it the probe reads no surface and the remedy would not fire anyway.
// What this fixes is the population that is NOT descending - the one that sits
// under the terrain with a flag nothing will ever clear - and on the measured
// rows that is the population the drive was losing.
//
// AND IT CANNOT MAKE A REAL FALL WORSE, which is structural rather than
// hopeful. The remedy behind this gate needs a valid surface ABOVE the
// character at its own x and y (BelowTerrainNeedsRecovery) and no floor within
// a stride of its feet (FloorUnderfoot). A character falling in open air off a
// cliff has nothing overhead at its own x/y, so the gap is negative and
// nothing holds. Opening the gate cannot invent a remedy the reading does not
// authorize.
//
// ONE FOLD, READ IN TWO PLACES, for the reason ReadingStandsOnTheGround gives:
// the adapter builds FALL_GUARD_DESCENDING from these three answers and this
// gate decides on them, and two places is one too many the moment they have to
// agree. `baselineRebased` is FallBaselineVerdict::rebase - the guard putting
// the baseline back under the character's feet, which it only does for a
// character it did not measure falling.
bool MeasuredToBeFalling(bool guardMayInspect, bool coreWouldNotCharge,
                         bool baselineRebased);

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

    // Say it once, loudly, and stop trying FOR NOW. A repeated identical
    // condition is a bug in this rule or in the world, and either way silence
    // is worse than one warning a person can go and look at.
    //
    // This is the END OF THE LADDER as well as the answer to a live polygon.
    // When a lift has not stuck, this module cannot fix the character where it
    // stands, and saying so is the whole remedy: the escalation that used to
    // be here relocated the failure to another continent instead, and the
    // character was still below the world when it arrived (#188).
    //
    // BUT IT IS A COOLDOWN AND NOT AN ABANDONMENT, and that distinction is
    // what tonight's four deaths were bought with. The sentence this drives
    // has always read "GIVING UP until it has been clear for 600s", and for a
    // character that is genuinely under the world the condition never goes
    // clear, so the window never opened and the give-up was permanent. Three
    // hours on the dev realm 2026-09-09: 47 reports, 5 give-ups, and 4 deaths
    // at the kill plane at full health, out of combat, with nothing steering
    // them. Every one of those characters was in the state this rung had
    // stopped looking at.
    //
    // AND THAT STATE IS THE ONLY ONE A REMEDY CAN REACH. Once a character is
    // in free fall out of the world, the ordinary surface probe reaches sixty
    // yards and finds nothing, so there is no height to lift it to and no
    // remedy to apply; see the void catch on TerrainRecoveryStep for the one
    // narrow exception. A character sitting still under the terrain is the
    // last state in which anything can be done, and it is precisely the state
    // this rung used to abandon forever. So the ladder is re-armed once the
    // forget window has passed since the last remedy, whether or not the
    // condition ever went clear: one lift per ten minutes rather than one lift
    // and then silence until the character dies.
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
    // DOES ANY DIRECTION OUT OF HERE PASS THIS MODULE'S OWN FOOTING CHECK?
    // The adapter walks a short stride on four bearings and asks each one the
    // same GroundHolds and NothingInTheWay every travel step is asked, so this
    // is the module trying to leave and reporting whether it could.
    //
    // TRUE BY DEFAULT, and that default is the safe one: see
    // StandingOnTheGround for why this can only ever take a reading AWAY from
    // "on the ground" and never toward it. A caller that does not measure it
    // gets exactly the behaviour this drive had before #262.
    bool footingHolds{true};
    // AND THE FLOOR, WHICH IS THE ONE THING NOTHING HERE EVER ASKED ABOUT
    // (#296). Every other reading on this struct is about the neighbourhood or
    // about the sixty yards OVERHEAD. `surfaceAboveZ` cannot be the floor a
    // character fell through, because the adapter probes downward from sixty
    // yards up and stops at the first thing it hits: a bridge deck, an abbey
    // roof, an Ironforge ceiling. This is the ground level at the character's
    // OWN x and y, looked for downward from its own feet, and it is the only
    // reading that can tell a character standing under a roof apart from one
    // falling through the void. They are identical in every other field.
    float floorBelowZ{0.f};
    // Separate from the number for the same reason `surfaceValid` is: the core
    // has two invalid-height sentinels and neither is a position.
    //
    // FALSE BY DEFAULT, and that default preserves the old behaviour exactly.
    // "No floor found" is what the edge of the world looks like from here, so
    // an unmeasured reading declines nothing and the ladder runs as it did
    // before #296. The asymmetry is the same one StandingOnTheGround argues
    // for: this can only ever take a lift AWAY, never authorize one.
    bool floorBelowValid{false};
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
    // HOW CLOSE A FLOOR HAS TO BE TO COUNT AS THE ONE THIS CHARACTER IS
    // STANDING ON (#296). A stride, not a search: the question is "are my feet
    // on something", and a floor two yards down is something a character is
    // standing on while one thirty yards down is something it is falling
    // toward. ZERO DISABLES IT and restores the behaviour this drive had
    // before #296, which is why it is a limit and not a literal.
    float footingReach{0.f};
    // HOW FAR ABOVE THE KILL PLANE THE LADDER'S BOUND STOPS APPLYING (#188).
    //
    // Everything else on this struct is a bound on how much this module may
    // do to a character that is probably fine. This one is the opposite: it
    // names the band in which a character is certainly NOT fine, because
    // VOID_PLANE_Z is where the core deals it max health outright and no
    // aura, immunity or fall arithmetic touches that. Inside the band the
    // bound is lifted, since the only thing the bound can buy there is a
    // corpse.
    //
    // DERIVED FROM THE POLL, NOT PICKED. The band has to be at least as deep
    // as a free fall covers between two polls, or a falling character steps
    // straight over it and is dead before the next reading. Measured on the
    // six void deaths of 2026-09-08/09, from the last sampled position to the
    // recorded one: 241, 60, 150, 241, 211 and 151 yards in one poll
    // interval. The worst is 241.
    //
    // AND BOUNDED ABOVE BY WHERE THE ROSTER CAN LEGITIMATELY STAND. Two
    // hundred and fifty yards over the plane is z -250, and the deepest floor
    // any of these characters has been measured standing on is -105.83, the
    // bottom of the Wailing Caverns shaft mod-dungeon-clear drops the party
    // down. So the band cannot contain a place a character is meant to be,
    // which is what lets the bound be lifted inside it without lifting it
    // anywhere a scripted traversal happens. ZERO DISABLES IT.
    float voidCatchYards{0.f};
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
    // ONE CATCH PER ENTRY INTO THE BAND ABOVE THE KILL PLANE (#188), and this
    // is the whole of that rung's bound. It is set when the catch fires and
    // cleared by any poll that reads the character above the band, so a
    // character that falls, is caught, and falls again is caught again, while
    // one the catch could not move is not teleported once a second for the
    // rest of its life. The core's gravity covers under ten yards in the first
    // second of a fall, so a catch that worked always leaves the band before
    // the next poll and a catch that did nothing never does: the bound
    // separates the two without needing a clock.
    bool caughtBelow{false};
    // WHERE THIS EPISODE STARTED. Anchored on the first poll that holds, and
    // the episode is abandoned when the character turns up on another map or
    // more than `episodeRadius` away.
    bool anchored{false};
    uint32_t mapId{0};
    float x{0.f};
    float y{0.f};
};

// IS THIS CHARACTER STANDING ON THE GROUND, or only near some ground?
//
// `hasLocalNavmesh` is what Detour answers when it is asked for a path a
// couple of yards away, and what it answers is "a polygon was found inside the
// search box". That is a statement about the neighbourhood, not about the
// feet. The core's own poly lookup searches five yards above and below a point
// before widening to fifty (PathGenerator.cpp:233, :247), so a polygon several
// yards OVERHEAD, on a patch joined to nothing, answers this question yes.
//
// MEASURED (#262). Four of the five family members stood at map 1
// (-605.64, -2106.66, 44.97) beside the Wailing Caverns approach ramp for the
// whole of two 12 minute staging attempts. There is no navmesh polygon under
// their feet at all: the only surface at their x and y is 34.4 yards over
// their heads and belongs to a different connected component from the door,
// and the nearest polygon of any component is an isolated patch 3.8 yards
// away, 1.4 along and 3.5 UP. That patch sits inside Detour's search box, so
// the recovery drive read a live polygon, logged that the character was
// "STANDING ON THE GROUND", and moved nobody. The travel drive was saying, in
// the same minutes, that "there is no direction out of where it stands that
// does not step off something".
//
// TWO INSTRUMENTS DISAGREED AND THE WEAKER ONE WAS BELIEVED. A polygon inside
// a search box is circumstantial: nothing about it says the character is on
// it, or that anything joins it to anywhere. A footing check that walked every
// bearing it has and refused all of them is this module trying to leave and
// failing. So the navmesh answer is trusted as "on the ground" only while the
// ground it reports can be walked off; ground no step can be taken from is not
// ground this character is standing on, whatever the mesh holds nearby.
//
// IT CAN ONLY EVER TAKE THE ANSWER AWAY. No polygon is still no polygon
// however well the footing holds, so this never invents an "on the ground"
// Detour did not report. That asymmetry is deliberate and is the same one the
// comment on TerrainRecoveryStep already argues for: the TRUE answer is the
// one that carries weight, so the only correction worth making is to the true
// answer.
bool StandingOnTheGround(bool hasLocalNavmesh, bool footingHolds);

// IS THERE A FLOOR UNDER THIS CHARACTER'S FEET?
//
// THE QUESTION THE DETECTOR NEVER ASKED (#296). Everything else this rule
// reads is about what is over the character's head or what Detour thinks of
// the neighbourhood, and neither can separate the two cases that matter: a
// character standing on the Ironforge floor under a ceiling forty yards up
// produces the same numbers as a character falling through the void with the
// same ceiling above it. A floor at its feet separates them, and nothing else
// on the reading does.
//
// MEASURED, TWICE, IN THE MODULE'S OWN LOG. On 2026-09-07 a character was
// lifted from map 0 (-3827.9, -831.9, z 10.1) to z 26.6 and was back at
// (-3828.1, -831.9, z 10.1) seven seconds later; and lifted from
// (-4895.6, -1004.7, z 503.9) to z 515.5 and was back at
// (-4895.4, -1004.7, z 503.9) two seconds later. Same x, same y, same z, to a
// tenth of a yard. It fell back onto the floor it had been standing on the
// whole time, and the module recorded that as "a lift at these coordinates did
// not stick" and climbed its ladder.
//
// BOUNDED IN BOTH DIRECTIONS, and the upper bound is not paranoia. The
// adapter's probe searches downward from slightly above the feet, so it can
// return a surface a fraction ABOVE them; that is still a floor underfoot. It
// must never be able to return one far above them, because "a surface a long
// way over my head" is the exact false positive this whole issue is about, and
// a predicate that accepted it would have re-implemented the bug it is here to
// fix. So the test is on the magnitude of the separation and not on its sign.
//
// AND AN INVALID READING IS NOT A FLOOR. `floorBelowValid` false means the
// probe found nothing in reach, which is what the edge of the world, a hole in
// the terrain and a fall of more than fifty yards all look like from here.
// Every one of those is a character this rule should still be allowed to help,
// so an unanswered question declines nothing. A reach of zero says the caller
// is not asking, which is the pre-#296 behaviour and has to be writable.
bool FloorUnderfoot(float currentZ, float floorBelowZ, bool floorBelowValid,
                    float reach);

// THE WHOLE "IS IT ON THE GROUND" QUESTION, FROM ONE READING.
//
// Two independent instruments, folded once so they cannot be folded twice
// differently. Detour's answer about the neighbourhood, corrected by the
// footing fan (#262), OR a floor at the character's own feet (#296). Either is
// sufficient; neither is necessary; and both can only ever say "on the
// ground", never "not".
//
// IT EXISTS SO THE ADAPTER AND THE STEP CANNOT DRIFT. Both need this answer -
// the step to choose the remedy, the adapter to choose which of the two
// give-up sentences to log - and before #296 each computed it from the same
// two arguments in two places. Two places is one place too many the moment
// there is a third input, which is exactly how a guard ends up enforced on one
// path and not the other.
bool ReadingStandsOnTheGround(TerrainReading const& reading, float footingReach);

// DID ANY INSTRUMENT FIND GROUND HERE AT ALL?
//
// THE SAME TWO INSTRUMENTS, WITH THE #262 CORRECTION TAKEN OFF, and that is
// the entire difference from ReadingStandsOnTheGround above. It exists
// because the correction is only sound in one direction and was being read in
// both.
//
// WHAT THE CORRECTION IS FOR. The footing fan answers "can this character
// walk out of here". A polygon nobody can step off is not ground this
// character is standing on, so the fan may take an "on the ground" away and
// open the ladder, which is right: a character sealed in a pocket beside the
// Wailing Caverns ramp needs SOMETHING tried, and four of them sat for 24
// minutes because nothing was (#262).
//
// WHAT IT IS NOT FOR. The far end of that same ladder is a sentence saying
// the character is below the world, this module is out of remedies, and
// somebody needs to look at what is under these coordinates. A refused
// footing fan is not evidence for any of that. It says the character cannot
// WALK; it says nothing about whether the ground under it exists.
//
// MEASURED, ON TWO CHARACTERS THIRTY YARDS APART, INSIDE THE SAME FIFTEEN
// MINUTES on 2026-09-09:
//
//   'Grog' at map 1 (1396.5, -2797.9, 119.7), surface z 140.8, a local
//          polygon, but no direction out of here holds. OUT OF REMEDIES.
//   'Og'   at map 1 (1392.9, -2823.3, 110.1), surface z 141.7, ground at its
//          own feet. STANDING ON THE GROUND, nothing is being moved.
//
// Detour reported a polygon for both. The only thing that separated them was
// the fan, and the one it refused is the one that was declared below the
// world and left. So the loud sentence is spoken only where BOTH instruments
// came back empty, and where one of them did not, the give-up says which and
// names #262 rather than asserting a fall through the world it cannot
// support.
//
// IT DOES NOT SUPPRESS THE GIVE-UP, and it must not: a character in a pocket
// nothing can move is exactly what a person needs told. What it suppresses is
// a claim about the world that the readings do not carry.
bool SomeInstrumentFoundGround(TerrainReading const& reading,
                               float footingReach);

// IS THIS CHARACTER IN THE LAST STRETCH ABOVE THE KILL PLANE (#188)?
//
// VOID_PLANE_Z is where the core stops treating a character as a character:
// max health of DAMAGE_FALL_TO_VOID, dealt outright, past every immunity,
// with the combat log calling it a fall. Nothing this module knows about fall
// damage applies and nothing it can do afterwards helps, because what follows
// is a corpse and a graveyard - and on 2026-09-09 one of those graveyards was
// on a different continent from the rest of the family, which is the outcome
// the whole no-cross-map rule exists to prevent.
//
// SO THE BAND IS WHERE THE ARITHMETIC OF THE LADDER CHANGES SIGN. Above it,
// the risk of a remedy is that a character which was fine gets displaced;
// #188 measured 204 of those in six hours and the bound is the fix. Inside
// it, the risk of NOT applying a remedy is a certain death, and a character
// two hundred yards below anything the world has at its own x and y is not
// one of the false positives the bound was built for. `catchYards` is the
// depth of the band; zero means the caller is not asking, the same "zero
// disables" every other optional bound in this file uses.
bool NearTheVoidPlane(float currentZ, float catchYards);

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
// way round that fact allows: a false only opens the bounded ladder rather
// than authorizing a displacement outright.
//
// THE TRUE ANSWER IS NOW CHECKED RATHER THAN TAKEN (#262). It used to be
// trusted and never overruled, and this comment said so. Then a polygon
// several yards OVERHEAD, on a patch joined to nothing, answered true for four
// characters sealed in a pocket beside the Wailing Caverns ramp, and this
// function congratulated all four on standing on the ground while the travel
// drive was reporting that no direction out of it was walkable. So the true
// answer passes through StandingOnTheGround first, which is the only reading
// this function takes that is NOT the adapter's word for it. Nothing else
// changed: the correction can only ever take an "on the ground" away, never
// add one, so a false is still a false and the ladder below is still the
// bound.
//
// AND THE LADDER IS NO LONGER A ONE-WAY DOOR (#188). Two changes, and both
// come from the same measurement: on 2026-09-09 five characters reached the
// end of this ladder and four of them were dead at the kill plane within the
// hour, at full health, out of combat, with nothing steering them.
//
// FIRST, THE GIVE-UP EXPIRES. It used to end the episode's remedies for as
// long as the condition held, and the condition holds forever where a
// character really is under the world, so "giving up until it has been clear
// for 600s" meant "giving up" for exactly the population it was said about.
// The ladder is re-armed once the forget window has passed since the last
// remedy, so the worst case is one lift every ten minutes rather than one
// lift and then nothing. That is a fifth of the rate the unbounded loop of
// #188 ran at, and it is measured against a different population: the 204
// displacements that bound was built for were characters standing on the
// ground under an arch, and every one of those is now stopped by the
// on-the-ground gate above before it ever reaches a rung.
//
// SECOND, THE BAND ABOVE THE KILL PLANE IS NOT SUBJECT TO THE BOUND AT ALL.
// See NearTheVoidPlane. A character inside it is hundreds of yards below
// anything at its own x and y, is seconds from a max-health kill it cannot be
// healed out of, and is none of the false positives the ladder bounds. It
// gets the lift whether or not the ladder has anything left, once per entry
// into the band. The remedy is still a lift, so it is still same map, same x,
// same y: catching a character above the plane cannot split the family, and
// letting it cross the plane demonstrably can.
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

// A STEP DOWN MUST ALSO BE A STEP BACK (#262).
//
// The footing check walks a straight step in strides and asks at each stride
// how far the surface moved. It had two bounds and they were different sizes:
// a DROP of up to ten yards was approved, because ten is under the height at
// which the core starts charging for a fall, while a RISE of more than eight
// was refused as a rock face rather than a slope. So a character could be
// walked DOWN a nine yard stride that it would never afterwards be allowed to
// climb. That is a one way door, and a check that approves a step whose
// reverse it refuses can strand a character permanently.
//
// It stranded four, beside the Wailing Caverns approach ramp, for two 12
// minute staging attempts each, with the travel drive reporting every errand
// that no direction out of that pocket was walkable.
//
// SO THE BOUND IS THE SMALLER OF THE TWO, IN BOTH DIRECTIONS. That is the
// whole rule: a stride is walkable exactly when the stride back is walkable.
// It introduces no number, because a symmetric rule cannot have two of them,
// and the larger of the pair was only ever reachable in the direction that
// traps somebody. Everything the drop bound was chosen for survives the
// tightening: eight yards is still well under the height at which a fall costs
// health, so a drop this approves is still a free one.
//
// AND IT ONLY EVER RUNS OFF THE NAVMESH. The travel step asks the navmesh
// first and takes its route whenever there is one, so this bounds the
// straight-line fallback and nothing else. The fallback exists for exactly the
// places the mesh has no route over, which is exactly where the one way doors
// are.
bool FootingSampleHolds(float fromZ, float toZ, float maxDrop, float maxRise);

// WHAT A REFUSED BEARING STILL PROVED (#312).
//
// THE FOOTING CHECK WALKS A BEARING IN STRIDES AND THREW AWAY EVERY STRIDE
// THAT HELD. It refuses the whole bearing the moment one stride fails, so a
// sixty yard probe that held for eleven strides and broke on the twelfth
// returned exactly what a probe that broke on its very first stride returns:
// nothing at all.
//
// MEASURED OFF THE SHIPPED TERRAIN, at the coordinates of four consecutive
// refusals on 2026-09-08. A character on open hillside had its straight
// bearing refused at stride 5 of 15, with sixteen yards already sampled and
// held behind it, and its other three bearings refused at strides 7, 12 and
// 13 - 24, 44 and 48 yards proved and thrown away. Every failing stride in
// all four refusals was a RISE, between 8.8 and 16.9 yards; not one was a
// drop, and not one was ground the probe failed to find. The check had proved
// walkable ground in every direction it looked and reported that there was
// none.
//
// SO A REFUSED BEARING IS TRUNCATED RATHER THAN DISCARDED. The step ends at
// the last stride that HELD, which is strictly before the stride that did
// not, so nothing is approved that this check did not walk in software from
// the character's own feet. It can only ever SHORTEN a step. It approves no
// stride the old rule refused, widens no tolerance, and cannot give back a
// fall the old rule caught: the failing stride is still refused, it has
// merely stopped condemning the eleven that passed.
//
// AND A TRUNCATED STEP HAS TO BE WORTH TAKING. One stride of proved ground is
// not a walk. It is a character being edged toward the very thing this check
// exists to keep it off, and at a fifteen second poll it buys nothing. Two
// strides is the shortest reach the footing fan asks anything at - see the
// adapter's own AnyDirectionHolds, which gives that reason for that number -
// so two strides is the floor here as well.
//
// THE FLOOR IS ON THE TRUNCATION AND ON NOTHING ELSE, which is why
// `wholeBearingHeld` is an argument rather than something the caller folds in
// for itself. A bearing that held all the way is taken however short it is,
// byte for byte as before: the reach for a near aim is the remaining distance,
// so an aim seven yards off in open country asks for a seven yard step, and a
// floor applied to THAT would refuse the exact case GroundHolds' uphill retry
// was added for. This predicate may only ever refuse a step the old rule would
// have refused too, and the short-circuit is what makes that true rather than
// nearly true.
bool ProvenStepIsWorthTaking(bool wholeBearingHeld, float provedYards,
                             float minYards);

// A FOOTING REFUSAL IS A FACT ABOUT WHERE A CHARACTER STANDS (#312).
//
// WHAT THE OLD BOUND WAS, AND WHY IT WAS NOT ONE. The refusal was announced
// once per ERRAND, and the line promised that the errand's own twenty minute
// stall clock still bounded it. Both halves are scoped to the errand, and the
// errand's target is rewritten from OUTSIDE this module: by an aim writer on
// its own cadence, and, for a catch-up walk, by the leader's own position,
// which moves every poll. A changed target is a new errand here - it clears
// the "already said" flag and restarts the stall clock at zero.
//
// MEASURED. One character was refused on six consecutive polls across six
// minutes against five different targets: the same warning printed six times,
// and the twenty minute clock restarted five of those times. A bound that is
// re-armed faster than it can elapse is not a bound, and a line that promises
// one is worse than a line that promises nothing.
//
// SO THIS ONE IS ANCHORED TO A PLACE, which is what a footing refusal is
// actually about - the ground under this character's feet, not the errand it
// happens to be carrying. The episode survives every target change and ends
// the moment the character is somewhere else: the same discipline, and the
// same argument, as TerrainRecoveryState's anchor above. What it counts is
// consecutive polls refused WITHOUT MOVING, which is the only reading that
// means "this character is not getting out of here".
struct FootingRefusalState
{
    // Consecutive polls refused at this anchor. This is the bound.
    unsigned consecutive{0};
    // Where the episode started, and whether one has started at all.
    bool anchored{false};
    uint32_t mapId{0};
    float x{0.f};
    float y{0.f};
};

struct FootingRefusalVerdict
{
    // Say it: once per episode, which is neither once per errand (the old
    // rule, which a target rewrite reset) nor once per poll (a stream).
    bool sayIt{false};
    // Give the errand up. This character has been refused `limit` polls
    // running without moving a yard, so the next poll reads the same terrain
    // from the same feet and there is nothing left for this errand to do.
    bool giveUp{false};
    // How many polls this episode has refused, for the line that says so.
    unsigned consecutive{0};
};

// One refused poll. Re-anchors and restarts the count when the character has
// moved further than `episodeRadius` from where the episode began, or onto
// another map. A zero `episodeRadius` never re-anchors on distance and a zero
// `limit` never gives up, which is what a caller that wants only the counting
// asks for.
FootingRefusalVerdict FootingRefused(FootingRefusalState& state, uint32_t mapId,
                                     float x, float y, float episodeRadius,
                                     unsigned limit);

// A poll that was NOT refused ends the episode. Called wherever a step was
// found, so a character that gets a step and later stops getting one starts
// its count afresh rather than carrying half of an old episode into a new
// place.
void FootingHeld(FootingRefusalState& state);

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

// A DOOR AT THE BOTTOM OF A RAVINE IS REACHED BY A CORRIDOR, NOT BY A BEARING
// (#242).
//
// WHAT WAS WATCHED. On 2026-09-05 at 18:03 the party set out for the Wailing
// Caverns entrance and arrived on the high ground over it: 97 yards out and 152
// yards above the staging point, and no nearer after ninety seconds. #228's
// rule read that correctly as Overhead and closed the run naming the cause,
// which is the outcome that stopped the falls. Nobody died. But the run still
// could not be staged, because the refusal is about the LAST yards and the
// defect is in the route that led to them.
//
// WHERE THE WAY IN ACTUALLY IS, read off the same navmesh the core's own
// pathfinder reads: mmaps/0013336.mmtile, map 1 grid 33/36, which holds the
// door and the rim over it, and its neighbours, which hold the corridor - the
// terrace named below is on 0013335.mmtile, grid 33/35.
// The entrance sits on the floor of a ravine at z 16.8. Directly over it, at
// the same x and y, there is a second walkable surface at z 161.9 - which is
// the ground the party keeps standing on, and it is genuinely walkable, so
// nothing about arriving there is a pathfinding error to be corrected. The only
// walkable descent runs north-east of the ravine: a terrace at about
// (-705, -2045, 66.5), then east and south around the rim, then back west along
// y about -2185 to the door. From the terrace that is 465 yards of walking to
// cover 179 yards of straight line.
//
// The world's own data says the same thing twice over. waypoint_data path
// 138070, the world database's own patrol for the creature at guid 13807,
// walks (-642.07, -2185.48, 45.34) down to (-719.33, -2224.44, 16.96) - the
// bottom of that corridor, point for point. And the creature spawns descend the
// same line: (-602, -2178, 49.8), (-643, -2182, 45.1), (-694, -2193, 31.0),
// (-704, -2195, 26.4), (-682, -2232, 17.4). A spawn point and a patrol point
// are both standable ground asserted by somebody other than this module.
//
// WHY A BEARING CANNOT FIND IT. The core's pathfinder is bounded twice over: it
// searches with a pool of 1024 nodes (MMapMgr.cpp, the query is built with
// exactly that) and returns at most MAX_PATH_LENGTH polygons (148 under
// MOD_PLAYERBOTS). Asked for a point it cannot reach inside those bounds it
// answers with the NEAREST POLYGON it did reach, which is the same behaviour
// RoutedPathGoesWhereAsked below already exists to catch. Over a ravine, the
// nearest polygon it reached is the rim, and from the rim the next answer is
// the same rim. That is a fixed point, and it is where the party stood for
// ninety seconds.
//
// SO THE FIX IS A PLACE, NOT A RULE. A door whose corridor has been measured
// carries the point where that corridor starts, and the leader walks at that
// first. Nothing here tries to make general pathfinding descend a cliff, and
// nothing here weakens #228: the approach is still judged, still refused when
// it stalls, and still refused from above. It is judged against the leg being
// walked rather than against a point on the far side of a cliff.
//
// THIS IS DELIBERATELY NARROW. Three of the four doors in the portal table
// carry no corridor and need none: Deadmines, Shadowfang Keep and Stockades are
// all staged successfully on the dev realm today, and a corridor that has not
// been measured must not be invented. A row with no corridor gets exactly the
// behaviour it has now, which is what the Direct leg below means.

// Which of the two points the leader is walking at this poll.
enum class ApproachLeg : std::uint8_t
{
    // Straight at the staging point. This is every row that carries no
    // corridor, and every leader who is already past the one his row carries.
    Direct,
    // At the corridor's start first. The staging point is still where the run
    // is going; it is not where this leg ends.
    ToWaypoint,
};

// The readings the choice is made on. All three come from the same poll, so a
// caller that could not measure one could not measure any of them.
struct ApproachRoute
{
    // False for a portal row that carries no corridor, which is the answer for
    // (0,0,0) - the same "that is not a place" the staging point's own
    // StagingPointCheck already gives. No second flag is invented for it.
    bool hasWaypoint{false};
    ApproachGap leaderToWaypoint{};      // leader -> the corridor's start
    ApproachGap leaderToStagingPoint{};  // leader -> the door's staging point
    // How far the corridor's start is from the staging point, in three
    // dimensions. A property of the two written-down places and not of the
    // leader, so it is the same every poll of a run. Negative means "no
    // reading", the convention ApproachDistance already returns.
    float waypointToStagingYards{-1.f};
};

// The sticky half. A leg that has been walked is not walked again inside one
// run: without this the leader would be sent back up the corridor every time
// the descent took him briefly further from its start than he was when he
// reached it, which is most of the descent.
struct ApproachRouteState
{
    bool waypointPassed{false};
};

// WHICH POINT TO AIM AT, and the one place `waypointPassed` is ever set.
//
// It is set on two different facts, and both of them are needed. The first is
// arrival: the leader reached the corridor's start, so the leg is done. The
// second is that the leader is ALREADY NEARER THE DOOR THAN THE CORRIDOR'S
// START IS, on ground a walk can cover - a party standing at the door after a
// run, or one that came in some other way. Sending those back out to the
// corridor would be walking away from the run.
//
// The second test is guarded by the shape and not only by the distance, and
// that guard is the whole of it. The walkable surface directly over the Wailing
// Caverns door stands at z 161.9 against the staging point's 16.8: nought yards
// out and 145 above, which is 145 yards away in three dimensions. The corridor's
// start is 179 from the same point. So the rim - the one place this whole fix
// exists for - is THIRTY-FOUR YARDS NEARER THE DOOR than the corridor is, and
// on distance alone the corridor would be skipped precisely there. It is
// Overhead, so it is not.
ApproachLeg ApproachLegStep(ApproachRouteState& state, ApproachRoute const& route,
                            ApproachLimits const& limits);


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

// SHOULD THIS MEMBER BE HELD WHERE IT STANDS UNTIL THE BARRIER OPENS (#346)?
//
// WHAT ARRIVING AT A STAGING POINT ACTUALLY DOES TODAY. Nothing in this module
// releases an escorted member's errand when it arrives - that was fixed, and
// the branch that does it says so. Upstream ends the walk anyway: patch 0012's
// arrived place-aim calls ChangeToIdle, and NewRpgStatusUpdateAction turns
// RPG_IDLE into a randomly chosen status on the bot's next AI tick, two of the
// eight of which walk the character somewhere of its own choosing. So the only
// thing holding a party on a doorstep is the coordinator re-issuing the walk
// every five seconds, and what that produces is not a party standing still. It
// is a party orbiting. Measured on the dev realm 2026-09-08, four members
// sampled every fifty seconds while their barrier waited:
//
//     42, 47, 44, 45 -> 142, 142, 139, 139 -> 173, 171, 175, 178 -> 103, 100,
//     102, 104 -> 214, 215, 215, 214 -> 99, 95, 100, 100 -> 90, 89, 92, 91
//
// All four move together, which is correct - they follow the leader and the
// leader is the one wandering - and none of them ever settles. Three runs in a
// row spent their whole twelve-minute staging window that way.
//
// SO ARRIVING HAS TO STOP BEING A WALK THAT KEEPS BEING RE-ISSUED AND START
// BEING A HOLD. The caller has a hold register already (two reasons live in it
// as of #335) and this is the predicate that says when to put a member into it.
//
// IT IS DELIBERATELY THE BARRIER'S OWN READING OF THE SAME MEMBER, NARROWED.
// Every member this returns true for is one DungeonRunBarrierMet is no longer
// waiting on, and that is the invariant worth having: a hold that could fire
// for a member the barrier still wants moving would be this module stopping a
// character on its way somewhere it is needed. The converse is not true, and
// the one member it is false for is the whole reason this is a separate
// function rather than a reuse:
//
//   AN `inside` MEMBER SATISFIES THE BARRIER AND IS NEVER HELD. It is through
//   the door, on another map, and past the point entirely. Holding it would
//   mean this module pinning a character inside an instance to make a barrier
//   outside that instance open faster, which is not a trade anything here is
//   allowed to make.
//
//   AND IN COMBAT IS NEVER HELD EITHER, which is the same line #335 drew when
//   it left `flee` alone. A character held still in a fight is a character
//   killed by the hold. The barrier already refuses to open for a member in
//   combat, so nothing is lost by declining to hold one - and because the
//   caller re-asks this every poll, a held member that is pulled into a fight
//   stops being wanted and gets its movement back on the next one.
//
// UNMEASURED FAILS TO false, like every other reading in this file: a member
// this poll could not place is a member nothing should be pinning to a spot it
// has not confirmed the character is standing on.
bool DungeonRunHoldsAtStage(DungeonRunMemberState const& member,
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
    // The leader is off the outside map AND a crossing to it exists. Still not
    // walkable, and the run still must not open on this poll - but the reason
    // is now "not yet" rather than "not ever", and the two must not share a
    // log line. THE THIRD ANSWER THE COMMENT ABOVE PREDICTED (#241): the
    // caller opens a crossing on this one and gives up on OffOutsideMap.
    NeedsCrossing,
};

// `aCrossingExists` DEFAULTS TO FALSE so that every caller and test written
// before there were boats keeps its exact previous answer, and so that the new
// answer can only be produced by a caller that went and looked. A crossing this
// function assumed rather than was told about would be the accident the
// comment above warns of, in the other direction.
DungeonApproach DungeonPortalApproach(std::uint32_t leaderMapId,
                                      std::uint32_t portalOutsideMapId,
                                      bool aCrossingExists = false);

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

// ------------------------------------------- getting one member back out --
//
// A CROSSING WALKS A PARTY THROUGH A DOOR TOGETHER. THIS IS THE OTHER CASE,
// AND IT HAS NO PARTY LEFT IN IT: one or two members standing inside an
// instance whose run is over, with the leader outside, and nothing that owns
// them.
//
// WHAT IT COST, MEASURED ON INSTANCE MAP 43. Two members at
// (-163.5, 132.9, -73.7) - the entrance trigger's own landing point, to the
// decimal - unmoved for over an hour. An instance cannot be reset while
// anybody is in it, so those two characters were the whole campaign: every run
// after them ended `reset_failed` five minutes after it opened, over and over,
// with the run number never leaving 1.
//
// AND THE DOOR THEY COULD NOT FIND WAS OPEN. The way out is an areatrigger at
// (-172.2, 139.0, -66.6) with a radius of 12, and the straight-line distance
// from where they stood is 12.7 yards - which reads as outside it, and is why
// this looked like a walking problem. It is not what the server measures.
// Player::IsInAreaTriggerRadius compares the radius against
// WorldObject::GetDistance, which subtracts the character's own combat reach
// (scale * DEFAULT_COMBAT_REACH, 1.5 for a player at scale 1), so the reading
// that decides is 12.7 - 1.5 = 11.2, and 11.2 is INSIDE 12. They had been
// standing in their own exit for an hour. Nothing had asked it.
//
// ASKED OF THE SAME CENSUS THE CROSSING USES, taken against a door, so
// `through` means "on the far side of it" and a negative distance means "not
// on the door's map at all". A member on some third map is not on the wrong
// side of THIS door and is not this decision's business; a member this poll
// could not find is not counted either, on the same terms the adapter's own
// reset blockers already use - not in the world, and therefore not on the map.
//
// TWO ANSWERS RATHER THAN ONE LIST, because they are acted on differently and
// the difference is the whole reason to name it: one of them can be walked
// right now, and the other cannot be walked at all and matters just as much.
//
// AND IT IS ASKED IN BOTH DIRECTIONS, WHICH IS WHY IT IS NOT NAMED FOR ONE
// (#384). It was `DungeonRunEvacuation`, because the only caller was the
// reset walking stragglers OUT. #384 is the mirror image and the same
// question: a member that died inside and released to a graveyard OUTSIDE is
// on the wrong side of the entrance door, and what the caller needs to know
// about it is exactly what the evacuation needed - can it be walked to that
// door now, or is it a corpse somebody else owns. The crossing predicates
// above already share one shape for both directions and say why; two copies of
// this, one per direction, is how the second one rots.
//
// NOTHING ABOUT THE ANSWER CHANGED IN THE RENAME. `walk` is still alive, on the
// door's map and reachable by an aim; `wait` is still dead and going nowhere
// under its own power.
struct DungeonWrongSide
{
    // Alive, on the door's map, and reachable by an aim. Escort each of these
    // at the trigger itself.
    std::vector<std::string> walk;
    // On the door's map and dead. No aim moves a corpse, the revival drive owns
    // it, and whatever is waiting on this member waits for it either way - so
    // it is NAMED rather than walked, and named separately rather than being
    // quietly missing from a line about who is still on the wrong side.
    std::vector<std::string> wait;
};

DungeonWrongSide DungeonRunWrongSide(std::vector<DungeonRunEntryState> const& members);

// CAN AN AIM AT A DOORWAY EVER OPEN IT?
//
// An arrival tolerance is the distance at which a walk STOPS: the errand reads
// as finished and nothing carries the character any nearer. An areatrigger
// fires on the server's own radius check and on nothing else. So a tolerance
// that is not strictly tighter than the radius it is aimed at is a character
// parked outside its own door with the walk reported as a success, which is
// the failure the adapter's TRAVEL_ARRIVED_POSITION_YARDS was written for:
// arriving within twelve yards of a seven yard trigger is arriving OUTSIDE it.
//
// ASKED OF THE DOOR BEING AIMED AT rather than asserted once against the
// smallest trigger anybody remembered. The radius is a row in the world
// database, this module aims at four of them, and that row is the only thing
// that knows.
bool ArrivalReachesTrigger(float arrivalYards, float triggerRadiusYards);

// A DOOR AIM GOES ON THE FLOOR, NOT IN THE MIDDLE OF THE BOX (#376).
//
// An areatrigger's row position is the middle of its box. This module already
// knows that and says so where TRAVEL_GROUND_SNAP_YARDS is argued, and had
// never applied it to the one aim it writes AT a door. For the Wailing Caverns
// exit the middle of the box is 7.01 yards above the floor the game itself
// lands a character on, and both rows are quoted in the adapter's own portal
// table: areatrigger 226 stands at z -66.6471, while areatrigger_teleport for
// the entrance puts an arriving player at z -73.66 in the same chamber, 10.6
// yards away.
//
// SEVEN YARDS IN THE AIR IS NOT A PLACE, and the cost of pretending otherwise
// is not a rounding error. PathGenerator sets PATHFIND_FARFROMPOLY_END past
// seven yards from any polygon (PathGenerator.cpp:301), and
// RoutedPathGoesWhereAsked above refuses a route whose actual end misses the
// height asked for by more than five. So a route to the middle of that box is
// refused from EVERYWHERE on the map, at every distance, and the travel drive
// falls through to its greedy five-bearing fan for the whole journey. Outdoors
// that fan only ever walks the last few yards from a staging point and nobody
// notices. Inside a dungeon it is a straight line drawn through rock, and a
// party that had just cleared the place could not walk 125 yards back to its
// own door.
//
// IT IS ArrivalReachesTrigger IN THREE DIMENSIONS, and deliberately the same
// rule rather than a second opinion about doors. That one asks whether a
// character that has stopped `arrivalYards` from an aim is inside a trigger of
// radius `triggerRadiusYards`. Grounding keeps the trigger's x and y and moves
// only its z, so the character now stops `arrivalYards` away along the ground
// and `correction` below, and the question is that same question over the
// hypotenuse: sqrt(arrival^2 + correction^2) < radius. At a correction of zero
// it IS ArrivalReachesTrigger, which is what makes this an extension of that
// bound rather than a new one beside it.
//
// AND IT IS CONSERVATIVE ON PURPOSE. Player::IsInAreaTriggerRadius measures the
// radius against WorldObject::GetDistance, which SUBTRACTS the character's own
// size - 1.5 yards for a player at scale 1 - so the server is more permissive
// than this test by that much in every direction. A correction this accepts is
// inside the door with room the test never spends. For trigger 226 the sum is
// sqrt(5^2 + 7.01^2) = 8.61 against a radius of 12.
//
// WHAT IS REFUSED, AND WHY REFUSING MEANS KEEPING THE OLD AIM RATHER THAN
// AIMING NOWHERE. A door with no radius is a BOX trigger and this question has
// no answer for it; a probe that found no surface has nothing to offer; and a
// correction larger than the door would be relocating the aim rather than
// grounding it, which is exactly what TRAVEL_GROUND_SNAP_YARDS refuses in the
// general case. All three keep the trigger's own z, which is the behaviour
// every door had before this existed - so a door this cannot improve is left
// exactly as it was rather than broken in a new way.
struct DoorAimHeight
{
    // The z the aim should carry.
    float z{0.f};
    // Whether that z is the floor rather than the middle of the box. The caller
    // says so in its aim line: a door that never grounds is a door whose
    // surface probe or whose radius wants looking at, and that should be
    // legible from a log rather than only from a debugger.
    bool grounded{false};
    // How far the aim moved, for that same line. Zero when it did not move.
    float correctionYards{0.f};
};

DoorAimHeight DoorAimOnTheFloor(float triggerZ, bool haveGround, float groundZ,
                                float arrivalYards, float triggerRadiusYards);

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

// ------------------------------ how long busy may mean anything (#382) --
//
// `partyBusy` above is the input that outranks everything, and until #382
// nothing decided how long a caller was allowed to keep saying it. The
// adapter's watchdog answered "is anybody busy right now", stamped its
// patience clock and returned, on every poll, forever - so a single member
// whose combat flag never cleared held the clock down and the skips and the
// extraction below them were unreachable by construction. Measured: a run sat
// `active` for 152 minutes with one member flagged in combat, four idle at full
// health, and the party's positions identical to the yard over 95 of those
// minutes.
//
// SO THE HOLD GETS A CEILING, AND THE CEILING IS MEASURED FROM THE RUN'S LAST
// REAL PROGRESS RATHER THAN FROM THE START OF THE BUSY STRETCH. That choice is
// the whole robustness of this rule and it was made the second way first. A
// clock that restarts whenever nobody happens to look busy is a clock a
// flickering combat flag resets, and a fix that a flicker can turn back off is
// not a fix - it is the same 152 minutes with more code in front of it. Real
// progress is a boss credited or thirty yards covered, both of which a stuck
// party cannot produce and a working one produces constantly, so `advancedAt`
// only ever moves when something actually happened.
//
// The reading, then: a party that looks busy is believed, but only while the
// run it is busy inside has got somewhere recently. Busy and going nowhere for
// longer than the ceiling is not a slow party, it is a stuck flag.
//
// `advancedAt` is when the caller last saw that progress; the caller owns
// stamping it, because it is the only thing that can measure a boss or a yard.
// Zero means nothing has been recorded yet, which holds - the caller is on its
// first poll and about to stamp it. A `ceilingSeconds` of zero means no grace
// is wanted at all and the hold is never believed, the same way a
// `maximumSkips` of zero above means extract immediately: a bound of nothing is
// a bound, not a request for no bound.
bool DungeonClearBusyStillHolds(bool anyBusy, time_t advancedAt, time_t now,
                                time_t ceilingSeconds);

// ------------------------------------------- what counts as a run (#225) --
//
// DID THE PARTY ACTUALLY GET INTO THE DUNGEON? Measured 2026-09-05: a campaign
// of 100 runs on map 43 had two rows in overseer_dungeon_run and
// dungeon_runs_done reading 3. The third was a staging failure, twelve minutes
// of a barrier that never opened, and it had consumed a slot in the campaign
// without anybody ever standing on the instance map. At about twelve minutes an
// attempt a campaign of 100 finishes in nineteen hours having cleared nothing,
// and reports success.
//
// THE BAR IS THE INSTANCE MAP, and the run table already agrees with it: a run
// ROW exists at all only because the arming drive saw a roster character
// standing on that map. A run that never reached it is not a run, so it does
// not fill a slot in a campaign of a hundred.
//
// THE DEFAULT FOR A WORD THIS FUNCTION HAS NEVER HEARD IS "IT ENTERED", and
// that direction is chosen rather than fallen into. The vocabulary grows toward
// endings of real runs - the accounting migration already names 'complete' as
// the value #143 will add the moment a run has a goal to complete - while the
// outcomes that mean the party never got inside TOGETHER are a closed set, all
// of them written by this module at points in the state machine that come
// before the run is handed to the dungeon brain. An unknown word is far likelier
// to be a new way for a real run to end than a new way to fail before one
// starts, and counting a real run as no run is the failure that loses a
// campaign's progress silently. An empty outcome is also "it entered": that is
// what the cold-heartbeat close leaves on a row, and that row exists because
// somebody was on the map.
//
// AND 'split_failed' JOINS THAT SET RATHER THAN COUNTING AS A RUN (#384), which
// is the one member of it that has somebody standing on the instance map while
// it is written. That looks like a contradiction of "the bar is the instance
// map" and is not, because the bar was never one character: STAGED_INSIDE holds
// precisely until the census says EVERY member is through, and a run that never
// satisfies it is never handed to the clearing drive, never arms the dungeon
// brain, and clears nothing. Measured on the dev realm: a run adopted with one
// of five inside sat 'active' for over 36 minutes with four members inside not
// moving one yard and the fifth on another map, because it had died inside and
// released to a graveyard outside. Letting that spend a slot in a campaign of a
// hundred is exactly the defect #225 exists to have removed - a campaign that
// reports success having cleared nothing - and it is the same argument, one
// phase later.
bool DungeonRunEnteredTheInstance(std::string const& outcome);

// HOW MANY OF THE NEWEST ATTEMPTS IN A ROW NEVER GOT INSIDE, counting back from
// the newest and stopping at the first that did.
//
// WHY THIS HAD TO WIDEN WHEN THE COUNTER NARROWED. The consecutive-failure stop
// used to ask only about 'reset_failed', and a staging failure was bounded by
// something else: it consumed a campaign slot, so a staging that failed for a
// reason that kept being true ran out of campaign eventually. Taking that slot
// away (which is the fix #225 asks for) takes the bound away with it, so the
// stop has to cover every way of failing before entry or the fix trades a wrong
// count for a loop with nothing at the end of it. The same argument brought
// 'split_failed' in with #384: a run that cannot assemble its census inside
// spends no slot either, so nothing else would ever bound a party that keeps
// splitting on the same door.
//
// `outcomesNewestFirst` is exactly what the caller's `ORDER BY id DESC LIMIT n`
// returns, and the count stops at the first outcome that entered - so a
// campaign that has had one good run since its last failure starts its streak
// again from zero.
unsigned DungeonRunTrailingFailures(std::vector<std::string> const& outcomesNewestFirst);

// DOES THE CAMPAIGN STOP RATHER THAN OPEN ANOTHER ATTEMPT (#306)?
//
// WHY THIS IS A NAMED DECISION AND NOT A `>=` AT THE CALL SITE, WHICH IS THE
// WHOLE POINT. The comparison was written once, inline, in the branch that
// decides a campaign's FIRST attempt - and every attempt after the first is
// decided somewhere else entirely, which never asked. Measured 2026-09-07 on a
// live realm: seven consecutive attempts that never reached the instance,
// against a threshold of three, and the stop's own log line absent from the
// whole of that worldserver's history. The counting above was right and the
// vocabulary beside it was right; nothing ever put the question. So the rule
// gets a name and one home, and every gate that opens a run calls it.
//
// A ZERO LIMIT IS "DO NOT STOP", NOT "STOP IMMEDIATELY". Zero reaches here only
// from a caller whose threshold could not be read, and reading an unreadable
// bound as "every campaign is already over" would end every campaign on a
// database that cannot answer - the opposite of the direction the run cap
// already refuses to guess in.
//
// `>=` AND NOT `>`: three consecutive failures is the third failure, not the
// fourth. The count comes from DungeonRunTrailingFailures above, so a streak
// broken by any run that got inside is already zero by the time it arrives.
bool DungeonCampaignStopsOnFailures(unsigned trailingFailures, unsigned failureLimit);

// WHERE A CAMPAIGN STANDS ONCE A RUN HAS ENDED.
//
// This is arithmetic and it is the part that was got wrong, so it is here where
// a test can pin it rather than inline at the one call site that does it. The
// trap is the last slot: run 100 of 100 fails to stage, `runNumber` reads 100,
// and a straight `finished >= wanted` declares the campaign done with
// ninety-nine dungeons actually cleared. The slot the attempt was aimed at is
// only filled by an attempt that entered.
struct DungeonCampaignProgress
{
    // Did this run fill the slot it was attempting? The one input that decides
    // everything else here, and the only one the caller may act on when it
    // writes dungeon_runs_done.
    bool counted{false};
    // How many runs of this campaign have now happened, which is what the
    // roster counter should read after this run.
    uint32_t runsDone{0};
    // Which slot the next attempt is for. Equal to `attemptedRunNumber` again
    // when this attempt did not count, because a slot nobody filled is still
    // the next slot to fill. 0 when the campaign is over.
    uint32_t nextRunNumber{0};
    // Has the campaign reached its cap? Always false when the cap is unknown:
    // "this database cannot answer" is not "the campaign is finished", and the
    // caller has its own branch for a cap it cannot read.
    bool campaignOver{false};
};

DungeonCampaignProgress DungeonCampaignAfterRun(std::string const& outcome,
                                                uint32_t attemptedRunNumber,
                                                uint32_t runsWanted,
                                                bool capKnown);

// ------------------------------------------ is the dungeon finished (#226) --
//
// THE QUESTION THE COORDINATOR COULD NEVER ASK, AND WHY IT CAN NOW.
//
// A run that goes perfectly has never had an ending it could reach. EXIT is
// entered from exactly two places, a stall the watchdog gave up on and an
// operator taking the job off 'dungeon', so a party that clears the whole
// instance simply stands in it until the map empties by some other means.
// Measured 2026-09-05: a confirmed 100 percent Wailing Caverns clear ran 121
// minutes and was recorded 'emptied', which is the row honestly reporting that
// the coordinator never walked them out.
//
// WHAT WAS LOOKED AT FIRST AND REJECTED, so nobody re-treads it. The obvious
// test is InstanceScript::GetEncounterCount plus GetBossState, and the run
// coordinator already carries a comment explaining that Deadmines' script never
// calls SetBossState, so its count is 0 and "all encounters done" would read
// TRUE the instant the party walked in. That is still true, and Wailing Caverns
// is the same shape: instance_wailing_caverns keeps a private _encounters[5]
// array behind SetData/GetData and never calls SetBossState either. So that
// framework cannot answer this for either map.
//
// WHAT ACTUALLY ANSWERS IT IS A DIFFERENT MECHANISM WITH THE SAME NAME. The
// completed-encounter MASK is not maintained by the instance script at all. It
// is maintained by the core off DungeonEncounter.dbc: KillRewarder calls
// Map::UpdateEncounterState, which walks
// sObjectMgr->GetDungeonEncounterList(map, difficulty) and, for the entry whose
// credit matches the kill, ORs in `1 << dbcEntry->encounterIndex` and writes the
// result straight back to the InstanceSave. That is the same mask this module
// already reads for its "a boss died" progress signal, and it rises on maps
// whose scripts do not use the boss-state framework at all. Wailing Caverns
// measured completedEncounters = 255 on all five characters, eight bits for
// eight credited encounters, on a script that sets no boss states.
//
// So the complete mask is not a per-map constant anybody has to write down. It
// is the OR of `1 << encounterIndex` over that same list, which the caller
// builds from the same store the core credits from. Asking whether every bit
// the map can credit has been credited is then exactly "every encounter this
// dungeon has, has happened".
//
// A MAP THAT CREDITS NOTHING IS UNKNOWABLE, NOT COMPLETE, and that distinction
// is the whole reason this returns three answers rather than a bool. An empty
// or missing encounter list means the DBC has nothing for this map, so the
// question has no answer here; reading that as "finished" would end every run
// on such a map the moment it started, which is precisely the vacuous-TRUE trap
// the boss-state route was rejected for. The caller keeps today's behaviour on
// an Unknowable map: the run ends the ways it already could.
enum class DungeonCompletion : uint8_t
{
    Unknowable,  // this map credits no encounters, so nothing here can be concluded
    NotYet,      // at least one encounter this map credits has not been credited
    Complete,    // every encounter this map credits has been credited
};

// `expectedMask` is every bit the map can credit; `completedMask` is what the
// save says has been credited. Bits set in `completedMask` that are not in
// `expectedMask` are ignored rather than treated as an error: the save is
// written by the core and outlives this module's opinions, and a bit from a
// difficulty this run is not on says nothing about this run.
DungeonCompletion DungeonRunCompletion(uint32_t expectedMask, uint32_t completedMask);

// THE WORD THAT GOES ON THE ROW, in one place because the vocabulary is now
// four wide at this one exit and picking it inline is how 'complete' would end
// up written for a run that stalled.
//
// The order is the priority. A run the coordinator PROVED finished is
// 'complete' whatever else was true of it; a run it walked out because the
// clearing watchdog gave up is 'stalled'; anything else that reaches the door
// is 'left', which is what this exit has always written. 'complete' needs no
// migration: the outcome column is VARCHAR(16) and was made one for exactly
// this, its own migration naming 'complete' as the value to add "the moment a
// run has a goal to complete".
char const* DungeonRunExitOutcome(bool provedComplete, bool stalled);

// --------------------------- the run yields to an errand it would trample --
//
// WHAT THIS IS ACTUALLY FOR, AND IT IS NOT ONLY THE TOWN TRIP (#168). Three
// passes outside the worldserver send the family to a counter and each of them
// writes `travel_npc`: the vendor pass, the bank pass, and now the maintenance
// trip. The run coordinator claims that same column the moment a run starts
// staging, and its claim is an unconditional write - so an errand written in
// the gap between two runs is taken back on the coordinator's next poll, and
// the character walks to a dungeon door instead of to the counter it was sent
// to. Nothing errors. The errand simply never completes, and its rows are
// answered "not in range" until they age out.
//
// That is why a hundred-run campaign has never had a maintenance trip, and it
// is why reading any of those three passes' success rate DURING a campaign is
// reading noise rather than a measurement.
//
// WHAT IS DELIBERATELY NOT CHANGED: the claim itself. Once a run is staging,
// the coordinator taking a straggler over from whatever it was doing is the
// thing that gets the party through the door, and it is working. This decision
// is asked only at IDLE, before a run has started, where the question is
// whether to start one at all. A run already under way never yields.
//
// SO THE RULE IS: a run does not OPEN on top of an errand somebody else is
// still running. Not "the run gives way", which would tear down staging that is
// working; just "the run waits its turn", which costs one cycle of a campaign
// that has ninety-nine more.

// WHICH COUNTER A TRAVEL AIM NAMES, AND `None` FOR EVERY AIM THAT IS NOT ONE
// (#378).
//
// THE VOCABULARY IS DECLARED ONCE HERE AND IsMaintenanceErrand IS ANSWERED IN
// TERMS OF IT, rather than the two carrying their own copy of the same three
// strings. They already had to agree: the run gate asks one of them about
// `travel_npc` and the travel drive's arrival branch now asks the other about
// the same column, and two lists that must agree is a shape this module has
// paid for before. A reader adding a fourth economy errand edits one function
// and both callers follow.
enum class CounterRole : std::uint8_t
{
    None,      // not a counter: a trainer, an innkeeper, an `at:`, a portal
    Vendor,    // "vendor" - what `sell` and `buy` need
    Banker,    // "banker" - what `bank` needs
    Repairer,  // "repair" - what `repair` needs
};

CounterRole CounterRoleForAim(std::string const& aim);

// Is this travel aim one of the economy passes' errands?
//
// The three roles are the same three the bridge treats as economy errands, and
// they are named rather than derived because the question is not "is this a
// role" - every value in this column is - but "did a pass that transacts write
// it". A trainer errand is somebody's profession and is not this.
bool IsMaintenanceErrand(std::string const& aim);

enum class MaintenanceHold : uint8_t
{
    Open,         // nothing is outstanding; the run may start
    Walking,      // the leader is walking to a counter and must not be turned round
    Transacting,  // rows are queued and unanswered; moving now loses the trip
    Overdue,      // held past the bound; the run starts anyway and says so
};

// Should the coordinator hold at IDLE rather than open a run?
//
// TWO CONDITIONS, BECAUSE AN ERRAND HAS TWO HALVES AND ONLY ONE OF THEM IS
// VISIBLE IN THE AIM COLUMN. While the family walks, `travel_npc` holds the
// role; the moment they arrive the travel drive releases it, and what is left
// is a queue of rows nobody has answered yet. A hold that watched only the aim
// would let a run start in the seconds between arriving and transacting, which
// is the worst possible moment to walk them away.
//
// AND IT IS BOUNDED, for the reason every wait in this file is bounded. The
// aim is written by a process outside the worldserver; if that process dies
// mid-errand the column can hold a role nothing will ever clear, and an
// unbounded hold would stop a hundred-run campaign with nothing in any log to
// say why. Past the bound the run opens anyway and the reason is said out loud
// once, which is the honest direction to fail in: a missed repair costs one
// run's durability, and a campaign that silently stopped costs the campaign.
MaintenanceHold DungeonRunMaintenanceHold(std::string const& leaderAim,
                                          unsigned outstandingErrands,
                                          time_t heldForSeconds,
                                          time_t boundSeconds);

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

// ------------ whether the staging walk is still the run's to measure (#367) --
//
// THE WATCHDOG ABOVE ANSWERS "IS THIS CHARACTER GETTING NEARER". IT CANNOT
// ANSWER "IS ANYTHING WALKING IT AT ALL", AND THAT TURNED OUT TO BE THE
// QUESTION. GATHERING claimed the leader's staging aim once per leg and never
// again, which made it the only walking mechanism in the module whose aim
// nothing renews: the barrier re-claims through its escort every poll, so do
// the crossing, the catch-up walk and the home errand. The leader on a staging
// aim is the one claimant with no escort entry - he is aimed, not escorted -
// so no sweep covered him, and every path that can end an errand ended his
// silently. His `grind`, `quest` and `move random` went back on with the
// travel focus that was swept with the errand, the follow drive re-granted him
// `new rpg` because he leads, and the phase then spent its whole twelve minute
// window measuring the distance to a staging point nothing was walking him to.
// Measured on the dev realm across three consecutive runs of one campaign: the
// gap to the leg being walked wandered between about 1000 and 1500 yards and
// the height relative to it changed sign, which is a character grinding rather
// than a character walking a route.
//
// AND THE ERRAND DEATH BREAKER ALREADY ASSUMED THIS WAS NOT SO. Its
// DeclineRunOwned case declines to release a run-owned errand on the stated
// grounds that a run re-claims its own aim within one coordinator poll, so
// calling it off there would change nothing. That was true of every claimant
// except this one.
//
// THE DECISION IS HERE RATHER THAN INLINE BECAUSE OF ITS THIRD ANSWER. Two of
// the three cases were already in the adapter as an `if` on the aim string; the
// one that was missing is the case where the string has NOT changed and the
// claim has gone anyway, which is the whole defect and is exactly the case an
// `if` on the string cannot see.
enum class StagingAim : std::uint8_t
{
    // The claim still stands on the leg being walked. Re-asserting it is a
    // no-op and there is nothing to say about it.
    Hold,
    // The leg being walked has changed, which is the ordinary handover from
    // the approach corridor to the staging point (#242). Not a fault.
    NewLeg,
    // The claim has gone from under a leg nobody changed. Something ended this
    // character's errand while the run was still waiting on it, and the run has
    // to take it back rather than keep measuring a walk that is not happening.
    Rearm,
};

// `legChanged` is "the aim string this poll wants is not the one this run last
// claimed". `runStillOwns` is "the aim this run wrote is still the errand in
// force" - which its caller has to read off BOTH of the registers that can
// answer it, because neither does alone: the claim register survives a poll the
// travel drive has not taken yet, and the travel drive's own record is the only
// thing that sees an errand written OVER rather than ended.
//
// AN ERRAND WRITTEN OVER MATTERS AS MUCH AS ONE ENDED, and it is the case a
// claim register on its own gets wrong. Something outside this module owns this
// column too. A row it rewrites is still a row, so nothing prunes it, and the
// claim register goes on naming an aim that is no longer what the character is
// walking to - which reads as a healthy run measuring a walk to a door while the
// character walks to a vendor.
//
// A LEG CHANGE OUTRANKS A LOST CLAIM, and the order is the rule rather than
// tidiness. A leg change re-claims anyway, and both answers can be true at once
// - the poll a leader reaches the corridor is a poll on which the old aim may
// also have just been released by arriving at it - so reporting that one as a
// fault would put a warning about a lost errand in the log every single time a
// run handed over from the corridor to the door, which is the normal thing that
// is supposed to happen.
StagingAim StagingAimStep(bool legChanged, bool runStillOwns);

// DOES THIS ANSWER START THE MEASUREMENT AGAIN? Both the ones that write a new
// aim do, and for one reason stated once: the watchdog's ladder measures the
// best distance to the thing being walked at, and after either of these that
// thing is not what the ladder has been measuring. #242 already made this
// clearing for a leg change, because the leader reaches the corridor at about
// four yards and the very next reading is his distance to the door. A re-arm
// needs it for the other half of the same argument: the rungs spent while
// nothing was walking the character were spent on nothing, and measuring a walk
// that has only just been given back against the old mark climbs straight to
// GiveUp on a leader that has only just been set off.
//
// ASKED RATHER THAN WRITTEN AS `aim != Hold` AT THE CALL SITE, so a fourth
// answer added later cannot silently inherit an argument nobody made for it.
bool StagingAimRestartsMeasurement(StagingAim aim);
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

    // Some part of a random suffix or property could not be turned into a
    // stat, so some of this item's worth is missing from `stats` and the
    // score is a floor rather than a figure. GearReadRandomProperty below is
    // what decides this, and the stats it DID manage to read are in `stats`
    // either way - a floor with more of the item priced into it is a higher
    // floor, and a higher floor settles more comparisons.
    bool unresolvedRandomProperty{false};
};

// ------------------------------------- what a random property is worth (#340) --
//
// A GREEN OFF A DUNGEON FLOOR CARRIES MOST OF ITS WORTH IN A NAME. "Scouting
// Tunic" is a stat-less template; the "+10 agility" that makes it a rogue's
// chest lives in a random property rolled onto the individual item, and the
// template says nothing about it whatsoever. Nineteen of the family's sixty-odd
// worn pieces were exactly that shape, and because nothing resolved them every
// one of those slots was worth its armour and its item level and nothing else -
// so `unresolvedRandomProperty` was set on all nineteen, the worn side of every
// comparison was a Floor, and GearCompare could prove nothing about any of them
// ever. Zero swaps in the log against fifty-three "cannot be settled".
//
// WHERE THE ANSWER ACTUALLY LIVES, and it is not a table lookup. When the core
// rolls a property onto an item it writes the property's enchantment ids into
// the item's own property enchantment slots (Item::SetItemRandomProperties),
// and that is the same place the core reads them back from when it applies the
// stats to the character (Player::ApplyEnchantment). So the caller does not
// have to walk from a property id to a property row to an enchantment; it can
// read the enchantments straight off the item it is already holding. Verified
// against the live realm: for all 77 of the family's randomly enchanted items
// the enchantment slots on the item match the property's own row exactly.
//
// WHAT THIS FILE THEREFORE NEEDS is not an item and not a DBC. It is the list
// of effects the caller read, as plain numbers, and one honest answer about
// whether it managed to read all of them.

// ONE EFFECT OFF ONE ENCHANTMENT, in the shape the core's own
// SpellItemEnchantmentEntry carries it: three parallel arrays of type, amount
// and argument. Plain ints, so this file still includes no core header.
struct GearEnchantEffect
{
    // ItemEnchantmentType. Only ITEM_ENCHANTMENT_TYPE_STAT, which is 5, is a
    // stat line this file can price. Everything else - an on-equip spell, a
    // resistance, a weapon damage bonus - is real worth that the score does not
    // model, and its presence is what makes the answer a floor.
    int type{0};

    // The size of it. For a STAT effect this is the stat's value, and the
    // caller has already done any scaling the core would do.
    int amount{0};

    // For a STAT effect, the core's ItemModType id - the same id
    // GearStat::type carries. Not a stat id for any other kind of effect.
    int stat{0};
};

// Only ITEM_ENCHANTMENT_TYPE_STAT can be turned into a GearStat, and the id is
// the core's own (DBCEnums.h:365-374). Named here for the same reason every
// other core id in this file is.
constexpr int ENCHANT_EFFECT_STAT = 5;

// What a random property turned out to be worth, and how much of it was read.
struct GearResolvedProperty
{
    // The stats to add to GearItem::stats. Every one of these is priced by the
    // ordinary weights; nothing about a stat is different for having arrived on
    // an item by a roll rather than in its template.
    std::vector<GearStat> stats;

    // Was ANY part of the property left unpriced? Goes straight into
    // GearItem::unresolvedRandomProperty, so the verdict is a Floor rather than
    // a figure - see GearConfidence below.
    bool unresolved{false};
};

// Fold what the caller read off an item's property enchantment slots into stats.
//
// `effects` is every effect on every one of those slots, in any order.
// `everyEnchantmentRead` is false when one of the slots named an enchantment
// the caller could not look up at all.
//
// THE STATS COME BACK EVEN WHEN SOMETHING IS UNRESOLVED, on purpose. A floor
// with the readable half of the property priced into it is a HIGHER floor than
// one without, and a higher floor is a lower bound that settles more
// comparisons. Nothing here can ever lower a score: a stat's weight is never
// negative and a rolled stat's value is never negative, so adding one can only
// move the number up, which is exactly what "floor" has to mean for #221's
// reasoning to hold.
//
// AN EMPTY READ IS NOT A FREE PASS. An item that claims a property and shows no
// effect at all has not been proved worthless - it has failed to be read, and
// 2011 of the 2012 property rows in the game's own data name at least one
// enchantment. So an empty list is unresolved, and the slot stays blocked,
// which is the right answer to "I could not tell".
GearResolvedProperty GearReadRandomProperty(std::vector<GearEnchantEffect> const& effects,
                                            bool everyEnchantmentRead);

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
    // The sibling hand-off reads it and wants the strict answer; only the swap
    // needed the finer one. The Need vote wanted the strict answer too, and is
    // why this is the strict one, but it no longer lives in this repository
    // (#374): the vote is cast upstream, on the packet that opens the roll, and
    // nothing here could reach one first. This field is written for the rule
    // rather than for one caller, so it is unchanged.
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

// ------------------------------------------ what an equip displaced (#372) --
//
// WHAT `kind='item_equip'` COULD NOT SAY, AND WHY IT COULD NOT. The row records
// the item that went on and the slot it went into, and the whole of its detail
// column is the literal string `slot <n>` - measured over all 453 rows ever
// written on the dev realm, whose distinct details are `slot 7`, `slot 14`,
// `slot 9`, `slot 15`, `slot 4`, `slot 8`, `slot 6` and `slot 2` and nothing
// else. An upgrade is a comparison and only one side of the comparison was
// ever stored, so the armory can list equips and can say nothing about
// progression: not what improved, not by how much, not which drop mattered.
//
// The measured cost of that is an ambiguity nobody can settle from the table. A
// roster character's row at 15:02 records one weapon going into slot 15 and the
// live inventory afterwards holds a different one. Either it was swapped
// straight back, or the equip never stuck, and the row is identical under both.
//
// WHY A SHADOW AND NOT A READ AT THE HOOK. The previous occupant is genuinely
// not reachable from the equip hook. Player::EquipItem visualises the new item
// into the slot (PlayerStorage.cpp:2844) BEFORE it calls OnPlayerEquip
// (PlayerStorage.cpp:2936), and Player::SwapItem has already called
// RemoveItem(dstbag, dstslot, false) (PlayerStorage.cpp:3971) before it calls
// EquipItem at all (PlayerStorage.cpp:3980) - so at the instant the hook runs
// the new item IS the slot's occupant and the displaced one is in neither the
// slot nor the bags. Reading the slot inside the hook returns what was just put
// on. The only place the answer still exists is a memory of what was there
// before, which is what these three functions maintain.
//
// AND WHY THE STAMP. A slot can also be emptied with nothing put back - the
// core's own AutoUnequipOffhandIfNeed does exactly that when a two-hander goes
// into the main hand - and an equip into that slot an hour later displaced
// nothing, it filled an empty slot. Both cases reach the shadow as "a clear,
// then later a fill", so the two are told apart by WHEN the clear happened.
// `stamp` is the core's per-world-update millisecond reading,
// GameTime::GetGameTimeMS, set once at the top of World::Update by
// _UpdateGameTime and constant for the whole of that update including the map
// phase every bot runs in. A clear and a fill inside one Player::SwapItem are
// consecutive statements with nothing between them, so they always carry the
// same stamp; a clear and a fill an hour apart never do. Where two genuinely
// different actions do land in one world update, calling the second a
// displacement of the first is the true answer anyway.
struct GearSlotOccupant
{
    unsigned entry{0};
    std::string name;
    // ItemTemplate::Quality (0 poor, 1 common, 2 uncommon, 3 rare ...) and
    // ItemTemplate::ItemLevel, denormalised at the moment of the equip. NOT
    // left to be looked up from `item_template` later: a content patch that
    // edits a template must not retro-date a judgement about what happened
    // today, and a reader that has to join to answer "was this an upgrade" is
    // the second round trip #372 exists to remove.
    unsigned quality{0};
    unsigned itemLevel{0};
};

// UNOBSERVED IS NOT EMPTY, and keeping them apart is the whole reason this is
// three states and not a bool. A module that has never looked at a slot and a
// slot that has been looked at and is bare are different facts with different
// readings, and folding the first into the second would write "this was the
// character's first ever shoulder piece" over an equip that replaced something
// this module simply had not seen. The death context columns document the same
// rule for the same reason: unknown is never a plausible value.
enum class GearSlotState : std::uint8_t
{
    Unobserved,
    Empty,
    Occupied,
};

// What this module last saw in ONE character's ONE equipment slot. Lives in the
// adapter, one per roster character per slot, and is fed only by hooks that
// already fire: nothing here polls.
struct GearSlotShadow
{
    // Has anything ever looked at this slot for this character? Set by the
    // login seed and by either observation below.
    bool observed{false};

    // What is in it now, when `occupied`.
    bool occupied{false};
    GearSlotOccupant worn;

    // What was last taken OUT of it, and the world update that happened on.
    // Kept after the clear rather than discarded, because in a straight swap
    // the clear is the only place the displaced item is still named.
    bool removedValid{false};
    GearSlotOccupant removed;
    std::uint64_t removedStamp{0};
};

// The answer the equip row wants: what the slot held immediately before the
// item that has just gone into it.
struct GearSlotBefore
{
    GearSlotState state{GearSlotState::Unobserved};
    GearSlotOccupant item;   // meaningful only when state is Occupied
};

// The three words `overseer_event.prior_state` can hold for an item_equip row.
// An empty string is a fourth thing and means the row predates #372 - which is
// why the column defaults to '' rather than to 'unknown'.
namespace GearPrior
{
constexpr char const* Unknown = "unknown";
constexpr char const* Empty = "empty";
constexpr char const* Item = "item";
}  // namespace GearPrior

// The slot was emptied. `stamp` is the world update it happened on.
void GearSlotCleared(GearSlotShadow& shadow, std::uint64_t stamp);

// The slot's occupant, as read directly off a character. Used by the login seed
// for every slot including the bare ones, which is what makes a later "the slot
// was empty" a fact rather than an absence of evidence.
void GearSlotSeen(GearSlotShadow& shadow, bool occupied, GearSlotOccupant const& item);

// What did the item now in this slot displace? Must be asked BEFORE
// GearSlotFilled records the new occupant, because after that call the shadow
// describes the new state and no longer the old one.
GearSlotBefore GearSlotDisplaced(GearSlotShadow const& shadow, std::uint64_t stamp);

// An item went into the slot. Consumes the pending removal, whatever the answer
// above was, so one clear can only ever explain one fill.
void GearSlotFilled(GearSlotShadow& shadow, GearSlotOccupant const& item);

// The word for the column, and the detail sentence for the row.
char const* GearPriorWord(GearSlotState state);

// The existing detail is the literal `slot <n>` and the website reads it, so
// the slot number stays exactly where it is and the new half is appended. A
// reader that parses the leading `slot <n>` is unaffected; one that compares
// the whole string against `slot <n>` sees a longer string, which is why the
// machine-readable copy of all of this is in columns rather than in here.
std::string GearSwapDetail(unsigned slot, GearSlotBefore const& before);

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

// THE HEIGHT BELOW WHICH THE CORE KILLS, AND IT IS NOT A FALL (#323).
//
// `MIN_HEIGHT` is -500.0f (GridTerrainData.h:29). Map::GetMinHeight returns
// the tile's own minimum-height plane and falls back to that constant, and
// GridTerrainData::getMinHeight falls back to it again when the tile carries
// no flight bounds. MEASURED against the shipped map files in the running
// image: not one of the 687 map 0 tiles and not one of the 988 map 1 tiles
// carries MAP_HEIGHT_HAS_FLIGHT_BOUNDS - those exist only on Outland and
// Northrend - so on both of this roster's continents getMinHeight returns the
// constant at EVERY coordinate. The kill plane is flat, and it is -500.
//
// WHAT HAPPENS THERE IS A DIFFERENT EVENT FROM A FALL, and confusing the two
// has now cost five investigations. WorldSession::HandleMovementOpcodes
// (MovementHandler.cpp:506-521) tests the position against that plane and,
// below it, sets PLAYER_FLAGS_IS_OUT_OF_BOUNDS and deals
// `EnvironmentalDamage(DAMAGE_FALL_TO_VOID, GetMaxHealth())` before calling
// KillPlayer. That is full max health, dealt outright, and Player.cpp:819-821
// says in its own comment that DAMAGE_FALL_TO_VOID bypasses every immunity.
//
// SO NOTHING THIS MODULE DOES ABOUT FALL DAMAGE CAN PREVENT ONE. It does not
// read m_lastFallZ, so SetFallInformation and the whole fall-baseline guard
// are irrelevant to it. It does not consult HasHoverAura, HasFeatherFallAura
// or HasFlyAura, so the charging gate #291 narrowed the stand-down to is not
// on this path at all. It is not Player::HandleFall and it is not reached from
// there.
//
// AND THE CORE ITSELF CALLS IT A FALL, which is the single most expensive fact
// in this issue: Player.cpp:847 sends the combat log entry as
// `type != DAMAGE_FALL_TO_VOID ? type : DAMAGE_FALL`. A void kill is reported
// to everything downstream as fall damage. That is why every reader of these
// rows, this module included, has spent five fixes on the fall arithmetic.
constexpr float VOID_PLANE_Z = -500.0f;

// WHAT THE RECORDED DROP ACCOUNTS FOR. Takes the column exactly as written,
// where a negative value is the unsampled marker YardsFallen returns, so a
// caller reads the row it has rather than reconstructing the sample.
enum class FallAccount
{
    Unsampled,       // no sample: the column says nothing either way
    NoDrop,          // it ended level with, or above, where it was last seen
    TooShortToHurt,  // a real drop, under the distance the core charges for
    Survivable,      // would have hurt it, could not have killed it from full
    EnoughToKill,    // would have killed it from full health outright
    // IT DID NOT LAND. The body is below the plane above, so the distance in
    // the column is not the drop and the fall arithmetic does not apply to it.
    // Measured on the same rows: the recorded drop on these deaths runs 30 to
    // 300 yards while the character is 548 to 778 yards below the terrain at
    // its own x and y, because the column is a delta across one sample gap and
    // the descent began long before that gap. "fell 136.9 yards which is
    // enough to kill it" was true of neither half.
    VoidPlane
};

// `voidPlaneZ` IS ASKED FOR, NOT ASSUMED, and a value at or above zero means
// the caller is not asking - the same "zero disables" this file uses for every
// other optional bound. A kill plane is negative by construction, so there is
// no legitimate reading this convention swallows. `deathZ` is read only when
// it is being asked about, which is why it may keep a meaningless default.
//
// THE PLANE IS TESTED FIRST, BEFORE ANY ARITHMETIC. A body below it did not
// land, so every later branch would be answering a question about a drop that
// did not happen with a distance that is not it.
FallAccount AccountForFall(float recordedYardsFallen, float safeFallYards = 0.f,
                           float rate = 1.f, float deathZ = 0.f,
                           float voidPlaneZ = 0.f);
char const* FallAccountName(FallAccount account);

// ------------------------------------------------- a revival and a party --
//
// A REVIVAL MAY NOT PUT A CHARACTER ON A MAP ITS PARTY IS NOT ON, unless there
// is nowhere on its own map to put it (#241).
//
// WHAT HAPPENED. DriveStuckRevival has four escalations that end in a bind
// point, and every roster character binds at map 0 (-8950, -132). On the dev
// realm 2026-09-05 the party LEADER died twice at one Barrens graveyard inside
// the repeat window, took the fourth of those escalations, and arrived in
// Duskwood:
//
//   17:34:31  'Grug' resurrected at the nearest graveyard
//   17:35:43  'Grug' twice at one graveyard inside 300s means it cannot live
//             there, so it was sent to its own bind point instead
//   17:47     Grug map 0 Duskwood, and Bork, Grog, Og and Ugga all map 1
//
// NOTHING IN THIS MODULE CAN UNDO THAT, which is what makes it worse than a
// long walk. `follow` cannot cross a map, DriveCatchUp refuses to start on a
// cross-map gap and returns silently, and an `at:` aim cannot name a
// coordinate on another map. The measured result was eleven minutes of four
// followers standing still with no log line, and an operator teleporting the
// family back by hand three times in one day.
//
// AND THE PREMISE OF THE ESCALATION IS UNSOUND WHERE IT FIRED. "Twice at one
// graveyard inside 300s means it cannot live there" reads like a rare verdict.
// In hostile territory it is the ordinary outcome. Measured on overseer_death,
// 18:18:24 to 18:22:30, four minutes:
//
//   Bork  Horde Guard  35%   travel_target ''
//   Grug  Horde Guard  12%   'vendor'
//   Ugga  Horde Guard  34%   'at:1:172.864,-1704.09,93.5606'
//   Grog  Horde Guard  16%   'profession trainer'
//   Og    Horde Guard   4%   'at:1:172.864,-1704.09,93.5606'
//   Grug  Horde Guard  12%   'vendor'
//
// Six deaths, one killer, all in zone 17. Two of them were walking to
// (172.9, -1704.1), and the only creature within 45 yards of that point is
// entry 6491, a SPIRIT HEALER, 13.3 yards away: the aim is a graveyard. The
// Barrens is Horde ground, so its graveyards stand beside Horde guards, and
// for an Alliance party the loop closes on itself: die, revive at the
// graveyard, be killed by the guards standing at it. A repeat there is
// evidence about the ZONE and not about the graveyard, and a bind teleport
// answers neither. Being routed into hostile ground at all is #234 and is not
// this decision's business; not breaking the party in half over it is.
//
// SO THE RULE IS ABOUT THE PARTY'S MAP AND NOT ABOUT MOVING AT ALL, and that
// is a deliberate narrowing of the closure #188 used. A terrain recovery could
// be closed under "never change maps" outright because the character was ALIVE
// and standing somewhere: doing nothing was always available. A revival has no
// such luxury. One of these four escalations fires because `game_graveyard`
// holds no row for the map at all, which is every death inside an instance,
// and refusing to move there would restore the exact regression that branch
// was written for: a body lay in the Deadmines for 29 minutes while this drive
// ran and never considered it. "Never move" would be a worse rule honestly
// applied. "Never move somewhere your party is not, while anywhere on your own
// map exists" keeps the corpse recovery and stops the split.
enum class RevivalDestination
{
    // Revive on this character's own map, at whatever graveyard the caller
    // had. Chosen when a bind teleport would leave the party behind.
    Graveyard,

    // The bind point. Still the right answer when there is nothing on this
    // map, and when the bind is where the party already is.
    PartyBind,
};

// What the adapter measured about one revival that wants to escalate.
struct RevivalMove
{
    // Where a bind teleport would land. The caller resolves this to the
    // LEADER'S bind when the character is grouped, so that a party which does
    // all take this exit lands together rather than scattered across two
    // starting zones.
    uint32_t bindMapId{0};

    // The map each OTHER member of this character's group is on, one entry
    // per member. Empty when it is not grouped, or when nobody else could be
    // resolved, and that is deliberately not the same as "the party is on map
    // 0": an unknown party map may not be used to refuse anything.
    std::vector<uint32_t> partyMapIds;

    // Is there ANY graveyard on this character's own map to revive it at
    // instead? Not "a safe one" and not "a different one": the question this
    // rule asks is whether an alternative exists at all, because the thing it
    // is weighed against is a continent.
    bool graveyardOnThisMap{false};
};

struct RevivalMoveVerdict
{
    // May the caller take its bind teleport?
    bool mayMove{false};
    // And does taking it leave the character on a different map from its
    // party? Only ever true when `mayMove` is true and there was no
    // alternative, so this is the line that has to be shouted rather than a
    // reason to refuse.
    bool splitsParty{false};
    // Whether a party map could be established at all, and which it is.
    bool partyMapKnown{false};
    uint32_t partyMapId{0};
};

// THE PARTY'S MAP IS THE MAP MOST OF THE OTHERS ARE ON, which is the reading
// that works whichever member is the one dying. Asking "the leader's map"
// gets the wrong answer in exactly the case that caused #241, because there
// the dying character WAS the leader and its own map was the one about to be
// abandoned. A tie keeps the first map seen, so the answer is stable for a
// given group rather than depending on iteration luck.
//
// FOUR RULES, IN THIS ORDER:
//
//   1. No party map known: move. An ungrouped character cannot split a party,
//      and a group nobody could resolve is not evidence of anything.
//   2. The bind is on the party's map: move. Nothing is being split; this is
//      the ordinary case for a family that binds where it plays.
//   3. The bind is elsewhere AND this map has a graveyard: DO NOT MOVE. The
//      alternative may be a graveyard that has already killed this character
//      once, and it is still the better answer, because a character revived
//      into danger beside its party can be helped, walked away or revived
//      again, and one revived onto another continent can do none of those and
//      cannot be reached by anything this module has.
//   4. The bind is elsewhere and this map has nothing: move, and say that the
//      party is now split. This is the instance case and the alternative is
//      leaving a corpse where it fell.
RevivalMoveVerdict RevivalMayCrossMaps(RevivalMove const& move);

// ----------------------------------------------- a follower and its leader --
//
// HOW FAR BEHIND ITS LEADER A FOLLOWER IS, AND WHETHER THAT IS A NUMBER AT ALL
// (#241).
//
// WHY THIS IS A DECISION AND NOT TWO EXPRESSIONS. The same pair of players was
// read twice in one loop, by two different expressions, and they disagreed.
// KeepRosterFollowing asked `p->GetDistance2d(leader) > FOLLOW_STALL_GAP_YARDS`
// with no map guard, and DriveCatchUp, called on the same two pointers on the
// very next line, folded a cross-map pair to a sentinel of -1. Measured on the
// dev realm 2026-09-05, with the leader in Duskwood and the followers in the
// Barrens:
//
//   17:42:30 WARN 'Grog' has not moved more than 10 yards in over 5 minutes and
//                 is 10560 yards from 'Grug' - clearing its movement so the
//                 next follow tick starts fresh
//   17:45:30 WARN 'Ugga' ... and is 9463 yards from 'Grug' ...
//   17:46:30 WARN 'Og'   ... and is 10642 yards from 'Grug' ...
//
// Those distances are not distances. They are two coordinate systems
// subtracted from each other, and the module acted on them: it cleared a
// movement generator every five minutes at three followers whose problem was
// not a stall. Over the same eleven minutes the other reading of the same fact
// produced nothing at all - not one "is N yards behind" line, not one
// "re-aimed at" line - because a cross-map gap of -1 takes the same exit as a
// follower standing in formation.
//
// THE SILENCE IS THE DEFECT, and it is worth being plain about what this
// decision does and does not do. It reunites nobody. `follow` cannot cross a
// map (FollowActions.cpp:285), the catch-up walk has nowhere on this map to
// aim at, and an `at:` aim cannot name a coordinate on another one
// (ResolveTravelTarget refuses a spawn off-map). All of that is correct and
// none of it changes. What changes is that a party split stops looking exactly
// like a party that is merely slow. An operator lost real time to that twice
// in one day, because eleven minutes of four characters standing still with no
// log line is indistinguishable from four characters walking.
enum class FollowGap
{
    // Not on the leader's map. NO DISTANCE IS MEANINGFUL HERE, which is the
    // whole reason this is a separate value rather than a very large number:
    // every reading downstream has to be able to refuse to answer.
    SplitAcrossMaps,
    // Close enough that upstream's own Follow() chases continuously and this
    // module has no opinion.
    InFormation,
    // Past the formation line, inside the range where walking to the leader
    // under its own aim is worth doing.
    Trailing,
    // Past anything `follow` will do for it.
    Stranded,
};

char const* FollowGapName(FollowGap gap);

// The two lines the reading is cut at. Both already exist at the call site and
// neither is introduced here: FOLLOW_STALL_GAP_YARDS is upstream's own
// SightDistance, the exact line inside which Follow() chases continuously
// (MovementActions.cpp:1180-1224), and FOLLOW_CATCH_UP_YARDS is five times it.
// Passing them rather than baking them in keeps this file free of the tuning
// and keeps the two call sites provably reading the same thing.
struct FollowGapLimits
{
    float formationYards{0.f};
    float catchUpYards{0.f};
};

// `sameMap` FIRST AND ALONE. When it is false the distance is not consulted at
// all, because there is nothing in it to consult: the caller may hand in a
// number it computed anyway, and this will not use it.
FollowGap ReadFollowGap(bool sameMap, float distance2d,
                        FollowGapLimits const& limits);

// Is this follower far enough back that a stall is worth acting on? False in
// formation, and false across a map boundary, where there is no distance to be
// far in. THE SECOND HALF IS THE FIX: nudging a movement generator is a remedy
// for a follower that has stopped walking, and a follower on another continent
// has not stopped walking, it has nowhere to walk.
bool FollowGapIsBehind(FollowGap gap);

// --------------------- what a follower cut off from its leader may do (#289) --
//
// THE BLOCK THAT WAS TOO WIDE. `follow` cannot cross a map, the catch-up walk
// has nowhere on this map to aim at, and an `at:` aim cannot name a coordinate
// on another one, so a follower on a different map from its leader stands still
// until somebody moves it. All of that is true and none of it changes here.
//
// WHAT WAS ALSO TRUE AND SHOULD NOT HAVE BEEN: the same follower could not walk
// to an innkeeper. Measured on the dev realm 2026-09-07, with three of five
// characters on the continent their dungeon is on and the leader on the other:
//
//   00:16:28 INFO 'Grog' was sent to 'innkeeper' but does not carry `new rpg` -
//                 nothing walks it anywhere. Followers travel by following the
//                 leader; aim the leader instead
//
// and, on the polls where the strategy was put on by hand, granted at 00:18:04
// and taken back nine seconds later:
//
//   00:18:13 WARN 'Grog' follows but carries `new rpg` with no escort asking
//                 for it - taking it back
//
// The errand refused there is the one that ENDS the split. All five of them
// bind at one point in the starting zone on the wrong continent, 0.68 yards
// apart, so every death drags the three who are already in the right place back
// across an ocean. A character walked to an innkeeper and bound there stops
// being dragged. It was blocked precisely when it was most needed.
//
// AND THE REFUSAL'S OWN REMEDY DOES NOT EXIST FOR IT. "Aim the leader instead"
// is true for a follower in formation, which arrives by following. Aiming the
// leader of a SPLIT party moves the leader, on the other map, and `follow`
// cannot cross a map (FollowActions.cpp:285). The advice names the one thing
// that cannot work.
//
// THERE IS NOTHING LEFT TO PRESERVE. The reason a follower may not steer itself
// is cohesion - five characters each free-roaming is the 937-yard scatter that
// taking `new rpg` off the followers cured - and a follower on another map has
// no cohesion to lose. It is not in formation, it is not healing or tanking for
// anybody, and `follow` is not moving it. It is standing still.
//
// SO THE QUESTION IS ABOUT THE ERRAND AND NOT ABOUT THE CHARACTER, and it is
// answered from the aim string alone, because that string already says which
// kind it is. Reachability is not the discriminator and never was: every target
// this module can resolve is on the character's own map by construction -
// ResolveTravelTarget refuses an `at:` whose map is not the character's,
// refuses a `trigger:` on another map, and skips every spawn with
// `spawn.mapId != mapId`. What separates the two kinds is whether the
// destination was chosen relative to the rest of the family.
//
//   * A POINT - `at:<map>:<x>,<y>,<z>` or `trigger:<id>` - is this module's own
//     coordination vocabulary and nothing outside it writes one. The catch-up
//     walk aims at where the LEADER stands, the dungeon approach aims at a
//     doorway, the staging barrier aims at a point picked for the party. Every
//     one of them is exactly what a split makes impossible.
//   * ANYTHING ELSE is a role keyword ("innkeeper", "repair", "vendor",
//     "banker", "profession trainer", ...) or a bare creature entry, and it
//     names a thing this character deals with by itself.
//
// ARRIVING IS ALL A TRAVEL ERRAND DOES, which is what makes the second bullet
// hold for the whole keyword list rather than most of it. The transaction at
// the far end is a separate command every time, so the two keywords that sound
// like they need other people do not: signing a guild charter at a `petitioner`
// is its own command, and a `flight master` is resolved against this
// character's own map and own team.
//
// FAIL CLOSED ON THE SHAPE IT CANNOT READ. An empty column is not an errand.
// A point aim is refused even though SOME point aims would in fact be walkable,
// an `at:` naming this character's own map being one: the narrower answer is
// the one that cannot let a split follower walk off under an aim the run
// coordinator meant for a party.
enum class SplitErrand : std::uint8_t
{
    // No errand at all: the column is empty.
    Nothing,
    // A place chosen relative to the rest of the family, and so out of reach in
    // the only sense that matters here.
    NeedsTheFamily,
    // An NPC on this character's own map, dealt with alone.
    SelfContained,
};

char const* SplitErrandName(SplitErrand errand);

SplitErrand ReadSplitErrand(std::string const& target);

// The form the call sites ask, so "may this follower steer itself" reads as a
// question about the errand rather than as a comparison against an enumerator.
// Three of them ask it and they must never disagree.
bool ErrandRunsAlone(std::string const& target);

// ---------------- and with NO errand at all, a cut-off follower still drives --
//
// #289 ASKED THE WRONG HALF OF THE QUESTION (#331), and the half it left out
// is the
// one the family spends its day in. That issue let a split follower keep
// `new rpg` WHILE it ran an errand it could finish alone, so the innkeeper trip
// that would end the split stopped being blocked by the split. Correct, and it
// only covers a character that has somewhere to be. The steady state of a split
// follower is an EMPTY errand column, and `ErrandRunsAlone("")` is false, so
// the backstop in KeepRosterFollowing takes the strategy straight back off.
//
// WHAT THAT COSTS, MEASURED ON THE DEV REALM 2026-09-08. Three of five roster
// characters sat on map 0 while their leader was on map 1. Sampled every three
// minutes with a forced save before each read, they did not move one yard and
// did not gain one point of experience in nineteen minutes:
//
//     10:06  Bork 26632  Grug 20498      (-10538,-44,43) / (-4914,-998,502)
//     10:25  Bork 26632  Grug 20498      the same two coordinates, to the yard
//
// The third, a priest frozen in a zone where mobs wander onto her, gained 180 a
// kill and nothing else - which is what "standing still" looks like when the
// world happens to walk into you, and is the only reason the number was not a
// clean five-way zero.
//
// THE REFUSAL HAS NO REMEDY TO OFFER, WHICH IS THE WHOLE ARGUMENT. `new rpg`
// comes off a follower because a follower that travels on its own outranks its
// own `follow` and wanders off - the scatter this module exists to prevent.
// That reasoning needs `follow` to be capable of doing something, and for a
// character on the other map from its leader it is not: this module says so
// itself, in capitals, once per split ("`follow` cannot cross a map ... nothing
// in this module can walk this follower to anybody until somebody moves it").
// Taking the strategy off such a character does not hold the family together,
// because the family is already apart and nothing here can rejoin it (#241,
// #274, #308). It only decides whether the character spends the wait levelling
// where it stands or standing motionless in a field.
//
// STILL REFUSED FOR AN AIM THE FAMILY OWNS, and that is the one property worth
// keeping from #289. An `at:` or `trigger:` target was chosen relative to the
// rest of the party - a catch-up walk, a doorway, a staging point - and a
// character that wanders off under `new rpg` while a run coordinator believes
// it is walking to a door is the failure the refusal was written for. So the
// verdict is the same one SplitErrand already draws, read the other way round:
// anything but NeedsTheFamily drives itself, and an empty column now falls on
// the driving side of that line instead of the frozen one.
bool SplitFollowerDrivesItself(std::string const& target);

// ------------------------- is the walk this drive would issue already running --
//
// THE GUARD THIS ANSWERS FOR, AND THE ONE THING IT USED TO GET WRONG (#293).
// DriveTravel re-issues a character's walk every poll. It must not do so when
// the character is already walking to exactly that destination, because
// ChangeToWanderNpc resets `lastReach` and `startT` and a walk re-issued every
// fifteen seconds never gets anywhere. So the drive asks the bot's own rpg
// state whether the walk is in flight, and skips the re-issue when it is.
//
// A STRATEGY CAN BE TAKEN OFF A BOT WITHOUT ITS rpgInfo BEING TOUCHED, and that
// is the hole. Only `new rpg` runs the action that walks a character to an NPC.
// Take that strategy away and the walk stops instantly, while the rpg state
// carries on naming the destination it was walking to. Read through that state
// alone, a character standing perfectly still reads as one in mid-stride.
//
// SOMETHING DOES TAKE IT, ROUTINELY. Measured on the dev realm 2026-09-07: a
// follower cut off from its leader was granted the strategy for its errand at
// 02:06:49, and at 02:08:05 the goal supervisor, which runs outside this module
// and writes a whole strategy set per character on its own cycle, wrote
// `nc -new rpg` and `nc +follow` for it in one batch:
//
//   02:08:08 INFO command 92255 ('nc -new rpg' for 'Og') applied
//   02:08:10 INFO command 92256 ('nc +follow' for 'Og') applied
//
// After which the travel drive said NOTHING about that character for ten
// minutes. Not a refusal, not a re-aim, not a release. It was online, at full
// health, on the errand's own map, with the errand column still set, three
// yards from where it had stopped. The grant that would have fixed it sits
// forty lines BELOW the guard, and the guard's silent skip never reached it.
//
// THE GRANT CANNOT MOVE, which is what makes the guard the thing that changes.
// A freshly granted `new rpg` starts at RPG_IDLE and the next tick turns
// RPG_IDLE into a randomly chosen status, two of which pick a point out of the
// world and walk to it. The grant has to be the statement before the aim is
// written or it opens a window for the character to wander off. So the drive
// must reach the grant, and the guard must stop claiming there is a walk to
// protect when there is not.
//
// HENCE THE THIRD INPUT, AND IT IS THE WHOLE FIX. "Already walking there" is
// three facts and not two: the destination has to match, the walk must not have
// been forced open by the staging watchdog, and the character has to be able to
// act on the walk at all. A character that cannot act on it has no walk in
// flight, whatever its own state says.
//
// FAIL OPEN, NOT CLOSED, WHICH IS THE OPPOSITE OF SplitErrand ABOVE, and the
// asymmetry is deliberate. Getting this wrong in the "no walk in flight"
// direction costs one redundant re-issue, which resets a clock. Getting it
// wrong in the other direction costs a character that stands still until the
// twenty-minute backstop gives up on it, and says nothing at all while it does.
// Those are not the same mistake and the cheap one is the one to make.
bool WalkAlreadyInFlight(bool reissueForced, bool canAct, bool atSameDestination);

// ------------------ who may be handed `new rpg` back on an errand (#311) --
//
// THE LEADER IS THE ONE CHARACTER THIS DRIVE WOULD NOT REPAIR.
//
// Only `new rpg` runs the action that walks a character to an aim, so a
// character on an errand that does not carry it does not move, however correct
// the row in `travel_npc` is. #295 taught the drive to notice exactly that and
// to take the strategy back on every poll until the errand ends. It gated the
// repair on "may this character steer itself", which is an escort or a follower
// cut off from its leader on an errand it can run alone. A LEADER IS NEITHER OF
// THOSE, by construction, so it fell through to a refusal written for
// followers:
//
//   00:20:24 INFO 'Og' was sent to 'at:1:-705,-2045,66.45' but does not carry
//                 `new rpg` - nothing walks it anywhere. Followers travel by
//                 following the leader; aim the leader instead
//
// measured on the dev realm 2026-09-08 against the party LEADER, whose roster
// row carries `lead` = 1. The remedy that line offers is the action that had
// already been taken. It is latched once per errand, so the drive then said
// nothing further about that character, and moved nobody.
//
// THE STRATEGY WAS NOT STOLEN. THIS MODULE TOOK IT, ON PURPOSE, THREE SECONDS
// EARLIER. The post-revival hold parks a character where it revives so that it
// does not walk straight back into whatever killed it, and a leader's `new rpg`
// outranks the `stay` that does the parking, so the hold takes the strategy off
// and gives it back itself:
//
//   00:20:21 INFO 'Og' is held where it revives for 20s (`+stay`, `-new rpg`)
//   00:20:45 INFO 'Og' is released from its post-revival hold (`new rpg`
//                 restored)
//
// The supervisor outside this module is ruled OUT rather than assumed innocent,
// because #293 taught this file to name a thief and the line #295 added prints
// "something outside it did" without ever checking. Over the three hours around
// this incident that supervisor wrote `nc +new rpg` to the leader sixteen times
// and `nc -new rpg` five times, and the last of the five landed more than two
// hours BEFORE the refusal. A strategy probe once a minute over the same window
// reports `new rpg` present on every one of fifty-six samples, including the one
// taken twenty seconds after the refusal was printed. On the measured poll the
// only writer that fits is this module's own hold.
//
// WHICH IS WHY A HELD CHARACTER IS ITS OWN ANSWER, and why this is a decision
// rather than one more condition on the grant. A held character must not be
// refused, because nothing is wrong with it and the refusal is a false alarm
// that latches for the rest of the errand. It must not be granted either,
// because a grant here is this module fighting its own hold: `stay` and
// `new rpg` are not siblings in the engine, so adding the strategy back does not
// lift the hold, it overrides it while leaving it standing. The third answer is
// to wait, and to say which of the two things it is not.
//
// THE ANTI-SCATTER RULE IS UNTOUCHED, and it is the whole reason this cannot
// simply grant to everybody. Five characters each carrying `new rpg` free-roam,
// which is the 937-yard scatter that taking the strategy off the followers
// cured. A follower in formation still gets the refusal and is still told to aim
// the leader, because for a follower in formation that advice is true and is the
// remedy that works.
//
// UPSTREAM ALREADY AGREES ABOUT WHO MAY CARRY IT. AiFactory adds `new rpg`
// behind `!GetGroup() || GetGroup()->GetLeaderGUID() == GetGUID()`, so "leads
// its party, or is in no party at all" is not a membership test invented here.
// It is the core's own, asked by the one drive whose errand depends on it.
enum class AimedMover : std::uint8_t
{
    // It carries the strategy already, so there is nothing to decide. Answered
    // rather than asserted: a caller that asks about a walking character should
    // get an answer back, not a crash.
    Walks,
    // This module is holding it still on purpose and will hand the strategy back
    // itself. Neither granted nor refused, and that difference is the point.
    // Answered for EVERY hold this module has, and there are three: the
    // post-revival one, a casting verb's (#335), and the one that keeps a party
    // standing on a staging point until its barrier opens (#346). One
    // enumerator rather than three because the answer a caller acts on is the
    // same for all of them - do nothing, the hold lifts itself - and a verdict
    // that named the reason would be a second place for the reason to drift.
    HeldOnPurpose,
    // It leads the party, or it is in no party. Take the strategy back, on this
    // poll and on every poll for as long as the errand lasts.
    GrantToLeader,
    // An escort, or a follower cut off from its leader on an errand it can run
    // alone. #122 and #289's case, reached through #295's repair, unchanged.
    GrantToSteerer,
    // A follower in formation. It arrives by following its leader, so aiming the
    // leader is advice it can act on.
    RefuseInFormation,
    // A follower on another map, which cannot arrive by following anybody and
    // whose aim names a place chosen for a party it is not standing with. #289's
    // case, unchanged.
    RefuseCutOff,
};

char const* AimedMoverName(AimedMover verdict);

// Everything the answer turns on, read by the caller from the world and from
// this module's own books. The defaults are the safe reading of "nothing is
// known": a character that carries nothing, leads nobody and steers nothing is a
// follower in formation, and that answer moves no one.
struct AimedMoverFacts
{
    bool carriesStrategy = false;
    bool heldAfterRevival = false;
    // THIS MODULE'S OTHER HOLD REGISTER IS HOLDING IT STILL RIGHT NOW (#335,
    // #346), whatever put it there. It is read here for the same reason the
    // first one is: the grant below hands back a mover, and handing one back to
    // a character that is three seconds into a summon channel is how 29 summons
    // in a row were refused `summoner is moving`. Every hold lifts itself, and
    // all of them are asked before any role is.
    //
    // NOT `heldToCast`, WHICH IS WHAT THIS WAS CALLED UNTIL #346. The register
    // behind it took a second reason that day - a member standing on a staging
    // point while its barrier waits for the rest of the party - and a fact
    // named after one of the reasons it reports is a fact the next reader
    // believes covers only that one. It never did: the caller has always
    // passed whatever the register said, and the register has never cared why.
    bool heldStill = false;
    bool leadsItsParty = false;
    bool steersItself = false;
    bool cutOffFromLeader = false;
};

AimedMover ReadAimedMover(AimedMoverFacts const& facts);

// Does this verdict hand the strategy over? The form the grant site asks, so
// that the two granting answers cannot drift apart from the single place that
// acts on them - the same discipline ErrandRunsAlone keeps above.
bool AimedMoverGrants(AimedMover verdict);

// ------------------------------------- crossing a map boundary (#241, #158) --
//
// THE FAMILY CANNOT WALK BETWEEN CONTINENTS, AND THAT IS CORRECT. Every aim
// this module writes for a place is `at:<map>:<x>,<y>,<z>`, and the resolver
// refuses one whose map is not the character's own, because MoveFarTo paths
// through PathGenerator and there is no navmesh across an ocean. None of that
// changes here.
//
// THERE IS A CROSSING, AND IT IS A BOAT. A MotionTransport walks its own taxi
// path, carries whoever stands on its deck by relocating them every tick, and
// teleports its passengers when that path changes map. Boarding is already
// solved by code that is already running: the bot AI polls
// `Map::GetTransportForPos` once a second and boards whatever transport the MAP
// says the character is standing on. This module must not board anybody, must
// not teleport anybody, and must not simulate a packet.
//
// WHAT THIS MODULE STILL CANNOT DO, WRITTEN DOWN BECAUSE THE FIRST VERSION OF
// THIS FILE CLAIMED OTHERWISE. It cannot get a character onto a deck. Upstream
// boards a follower with a straight-line `MovePoint(generatePath = false)` over
// the last sixty yards, and it only ever does so because the MASTER is already
// aboard and supplies the point (FollowActions.cpp). There is no navmesh on a
// moving transport, so an `at:` aim cannot path onto one, and the party leader
// has no master to be pulled aboard by. A transport's stop frame is not a
// substitute: it is the SHIP's own world-space origin at its mooring, which is
// over water beside a pier rather than anywhere a character may stand.
//
// SO EVERY LEG THAT NEEDS A PLACE TO STAND IS FAIL-CLOSED UNTIL SOMETHING
// SUPPLIES ONE. The first version of this decision took a stop frame as a
// walkable berth and would have aimed the family at a mooring. It is now the
// caller's job to hand in a berth that the WORLD has agreed is standable, and
// a berth that was not handed in is a refusal that says so. That is the #121
// discipline applied to the one place this file could still have broken it: a
// coordinate nobody validated is not a destination, however exactly it was
// read out of the right table.
//
// THE UNITS OF THIS DECISION ARE MEMBERS, NOT THE PARTY. The party it was
// written for was ALREADY split when the crossing became necessary: three
// followers on the destination map beside the dungeon door, the leader and one
// follower on the far continent. "Assemble, then cross together" would have
// had nothing to say to it. Each member is read against the DESTINATION.
//
// ONLY THE LEADER IS EVER AIMED, which is the standing rule this repository has
// already paid for. The followers have a drive that walks them to their leader
// on their own map, and upstream's boarding assist takes them onto a deck their
// leader is standing on. The leader is therefore both the only character this
// aims and the only character whose boarding is unsolved.
enum class CrossingLeg : std::uint8_t
{
    // Nothing readable enough to name a leg. The world is not answering.
    Unknown,
    // Somebody is on a map that is neither end of this crossing.
    OffRoute,
    // On the origin map and not aboard: the berth is the next place to be.
    WalkToBerth,
    // At the berth, with nothing to do but wait for the transport. ITS OWN LEG
    // BECAUSE THE ARRIVAL IS WHERE THE LAST VERSION LOOPED: the travel drive
    // releases an `at:` errand at five yards, this reading called it Walk all
    // the way in, so the aim was released and reclaimed forever, and every
    // reclaim restamped the errand clock that the death breaker measures its
    // window from. A leader standing at the berth is not walking to it.
    WaitForTransport,
    // The LEADER is on the deck, or riding. The transport owns the crossing.
    Aboard,
    // On the destination map and STILL A PASSENGER. A distinct leg from Ashore
    // because "the boat has arrived" and "the family is off the boat" are two
    // facts, and treating the first as the second ends the crossing with
    // characters standing on a deck that is about to sail back.
    Disembark,
    // Every member read on the destination map and off every transport.
    Ashore,
};

char const* CrossingLegName(CrossingLeg leg);

enum class CrossingAction : std::uint8_t
{
    // A fact needed to decide was missing. Do nothing, and say which.
    Wait,
    // The crossing cannot be made, and waiting will not make it makeable.
    Refuse,
    // Aim the leader at the berth. The only action that moves anybody.
    Walk,
    // At the berth. CLAIM NOTHING AND RELEASE NOTHING. The distinction from
    // Walk is not cosmetic: re-claiming an errand the traveller has already
    // finished is what pinned the death window to zero, because the travel
    // drive stamps a fresh errand clock whenever the target it sees differs
    // from the one it had, and it had just released this one on arrival.
    Hold,
    // The LEADER is aboard. DO NOTHING, DELIBERATELY: the transport is the
    // mechanism and anything issued now would fight it. A separate value from
    // Wait because "doing nothing because the boat is sailing" and "doing
    // nothing because the world did not answer" must not be one value.
    Ride,
    // Somebody is on the destination map and still a passenger. This module has
    // no way to walk them off, so this is a Wait that says something completely
    // different and must be visible as its own thing.
    Disembark,
    // Every member is on the destination map, off every transport. Over.
    Done,
};

char const* CrossingActionName(CrossingAction action);

// One member, as the adapter read it off the world.
//
// `readable` IS NOT `online`. It is "this character was steerable on this
// poll", the same gate every other drive here uses. An unreadable member is
// never counted as arrived: four of five seen on the far side says nothing
// whatever about the fifth.
//
// `aboard` IS READ FROM THE MEMBER, NOT FROM THE BOAT. The first version asked
// whether the member's transport pointer equalled a transport found by scanning
// the LEADER's map, so a follower genuinely riding a boat that was currently at
// the far dock read as not aboard, and flipped back when it returned. The
// adapter now asks the member what it is standing on and matches the route by
// transport identity, so the answer does not depend on where the boat is.
struct CrossingMember
{
    bool readable{false};
    bool isLeader{false};
    bool aboard{false};       // the member's own transport IS this route's
    std::uint32_t mapId{0};
    float berthDistance{0.f}; // yards, two-dimensional; meaningless off-map
};

// What the adapter could establish about the crossing itself. Every field is a
// fact the world was ASKED for, and false means "not established", never
// "established false".
struct CrossingWorld
{
    std::uint32_t originMap{0};
    std::uint32_t destinationMap{0};
    // A transport is known whose own path serves both maps.
    bool transportFound{false};
    // A place on the ORIGIN map that the world agreed a character may stand on
    // and from which that transport can be boarded. NOT a stop frame. Until
    // something can supply one, this is false and the crossing refuses.
    bool berthKnown{false};
    // The same, on the destination map, for walking off at the far end.
    bool landingKnown{false};
    // The berth was swept for spawns above the party's level, the same sweep at
    // the same radius a travel destination gets, and it is not clear.
    bool berthGuarded{false};
    std::uint32_t berthGuardLevel{0};
    // THIS CROSSING HAS TAKEN TOO LONG AND IS GIVEN UP ON. A fact rather than a
    // policy here: the caller owns the clock. It exists because the death
    // breaker declines to act on an errand a run owns, on the stated grounds
    // that the run's own stall handling will answer it - and every other
    // run-owned claim in this module has a backstop behind it while this one
    // shipped without any timer at all. An errand nothing can call off and
    // nothing can time out is an errand that kills a family slowly.
    bool overdue{false};
    // The transport's own mooring on each map, which is what a stop frame
    // actually is. Carried for the log line only: it says where the boat ties
    // up, so an operator reading a refusal can go and look. NEVER an aim.
    bool mooringKnown{false};
};

struct CrossingLimits
{
    // Inside this, the leader is AT the berth. An arrival tolerance, not a
    // boarding radius: this module never decides anybody is aboard.
    float berthArrivedYards{0.f};
};

struct CrossingStep
{
    CrossingLeg leg{CrossingLeg::Unknown};
    CrossingAction action{CrossingAction::Wait};
    std::size_t readable{0};
    std::size_t unreadable{0};
    std::size_t ashore{0};        // destination map, off every transport
    std::size_t waiting{0};       // origin map, not aboard
    std::size_t aboard{0};        // a passenger, wherever the boat is
    std::size_t stillAboard{0};   // a passenger AND on the destination map
    std::size_t offRoute{0};      // read on neither map
    bool leaderReadable{false};
    bool leaderOnOrigin{false};
    bool leaderAboard{false};
    bool leaderAtBerth{false};
};

// THE ORDER OF THE TESTS IS THE FAIL-CLOSED RULE, WRITTEN OUT.
//
// UNREADABLE FIRST, ahead of everything and ahead of Done in particular. This
// is the one branch that separates this from a decision layer that reads a
// missing member as a negative reading.
//
// DISEMBARK BEFORE DONE, because a character on the destination map that is
// still a passenger has not arrived: it is standing on a boat that is about to
// go back. Done requires every member ashore AND off every transport.
//
// THE LEADER'S OWN STATE DECIDES WHETHER ANYTHING IS AIMED, not the party's.
// The first version promoted the whole family to Ride as soon as ANY member was
// aboard, and the adapter's Ride branch released the leader's aim - so a
// follower stepping onto the deck one second early stopped the leader walking
// and the boat left without him, every circuit. A follower aboard is not a
// reason to stop aiming the leader, because the leader is the only character
// this ever aims and a passenger is never the one being aimed.
CrossingStep ReadCrossing(CrossingWorld const& world,
                          std::vector<CrossingMember> const& members,
                          CrossingLimits const& limits);

// The step as one sentence, including when the answer is "nothing". A refusal
// that does not say which fact was missing trains an operator to ignore it.
std::string CrossingExplanation(CrossingStep const& step, CrossingWorld const& world);

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

// -------------------------------------------------- the ground on the way --
//
// #300, which is #234 item 2 reaching its third statement.
//
// WHETHER A CHARACTER CAN GET THERE ALIVE, WHICH IS A DIFFERENT QUESTION FROM
// WHETHER IT CAN STAND THERE (#267 named this one, put it explicitly out of
// scope, and said a correct destination can still have a bad route).
//
// THERE IS NO ROUTE TO READ, AND THAT IS THE FINDING THIS IS BUILT ON. Nothing
// anywhere computes the path a long walk will take, so there is no polyline to
// sample and no cheaper reading being passed over. Read at the pinned core and
// the pinned mod-playerbots:
//
//   * PathGenerator is never even asked about a far destination. CalculatePath
//     returns a TWO POINT straight line, typed PATHFIND_NORMAL |
//     PATHFIND_NOT_USING_PATH, whenever the destination's navmesh tile is not
//     already loaded (PathGenerator.cpp:173-179, HaveTile at :817-831), and a
//     tile is loaded only when its 533 yard map grid is created
//     (GridTerrainLoader.cpp:9-17, :74).
//   * Even with both tiles loaded it cannot answer past about 296 yards: 74
//     points at a 4 yard step (PathGenerator.h:32-38), and a smooth path that
//     reaches that cap is DISCARDED rather than truncated, replaced by the same
//     two point shortcut (PathGenerator.cpp:660-690, FindSmoothPath's return at
//     :1065).
//   * The mover treats that shortcut as no answer and falls back to sampling
//     TWO RANDOM BEARINGS in a forward cone of plus or minus ninety degrees,
//     35 to 70 yards out, walking to whichever came back nearest the bearing of
//     the destination (NewRpgBaseAction.cpp:141-183). Then it does it again.
//   * This module's own long `at:` walk is the same shape by construction: the
//     navmesh refuses, so GroundedStep takes a TRAVEL_STEP_YARDS step along the
//     bearing to the aim, and the next poll steps again.
//
// So what a party actually walks between two far points is a chain of short
// hops aimed down the straight line, jittered by a forward cone and by local
// ground. The straight line is not an approximation of the route. It is the
// only structure the route has - and the followers, which are moved by a short
// step toward their leader and route nothing at all, walk it more exactly than
// the leader does. Sampling it is therefore honest rather than a heuristic.
//
// WHAT IS SAMPLED IS THEREFORE THE STRAIGHT LINE, AND THIS CAN ONLY EVER
// REFUSE. A walk whose line is lethal is lethal; a walk whose line is clear may
// still bend into something the line missed. That asymmetry is the honest limit
// of the instrument and is why this gate may only take a candidate away, never
// certify one. It is also what decides the rounding in PlanRouteSamples below,
// so read the two together.

// HOW A WALK IS CUT INTO SAMPLES. Arithmetic with a boundary in it, so it lives
// here where a test can compile it rather than in the adapter where nothing
// can.
struct RouteSampling
{
    std::size_t samples{0};
    // The spacing ACTUALLY used, not the nominal one asked for. It is at most
    // the nominal spacing and it divides the read length exactly, so
    // `samples * spacingYards == readYards` to the yard and the totals
    // JudgeRoute reports are the real length of ground rather than a multiple
    // of a round number that happens to be near it.
    float spacingYards{0.f};
    // How much of the line is read at all: the whole span, unless the span is
    // longer than the cap, in which case the tail past it is deliberately left
    // unjudged. Safe in exactly one direction, and only because this gate may
    // only refuse: an unread tail can hide a danger and can never invent one.
    float readYards{0.f};
};

// THE ROUNDING IS UP, AND IT IS UP FOR A REASON SPECIFIC TO THIS GATE.
// Truncating leaves the last fraction of a spacing with no sample standing in
// it, and that fraction is at the END of the line: the segment nearest the
// destination, the one a character walks last, and the one an unbroken hostile
// run beginning at the destination itself occupies. Because this gate may only
// ever refuse, an error there cannot produce a false refusal; it can only
// produce a MISSED one, which is the failure this whole rule exists to prevent.
//
// AND THE SAMPLES SIT AT THE MIDDLE OF THEIR SPACING, not at the far end of it,
// which is what makes the count and the yardage agree. `samples` spacings laid
// end to end are exactly `readYards` of ground, each with one sample in the
// middle of it: nothing at the near end is unspoken for, nothing at the far end
// is unspoken for, and no sample is ever placed BEYOND the destination, where
// it would be reading ground the party does not walk.
//
// A span of zero, a spacing of zero, or a span under one spacing all come back
// with no samples, which JudgeRoute reads as "not asked" rather than as "safe".
RouteSampling PlanRouteSamples(float spanYards, float maxSpacingYards, float maxSpanYards);

// Where sample `index` stands, in yards along the line from the character.
// Zero-based, and always strictly inside (0, readYards).
float RouteSampleAt(RouteSampling const& sampling, std::size_t index);

struct RouteReading
{
    uint32_t characterLevel{0};
    // How far apart the samples below stand, in yards, which is
    // RouteSampling::spacingYards and not the nominal spacing the caller asked
    // for. Each sample speaks for the spacing it sits in the middle of, so N
    // lethal samples in a row are N spacings of ground rather than N-1: the
    // question is how much ground the party has to survive, not how far apart
    // two readings were taken.
    float sampleSpacingYards{0.f};
    // The level of the worst thing this character could be made to FIGHT within
    // the threat radius of each sample, in order from where the character
    // stands to the destination, and zero where there is nothing. Measured by
    // the caller off spawn data rather than the live grid, for the reason
    // GRAVEYARD_THREAT_RADIUS gives: a destination two grids away is not
    // loaded, and an unloaded grid reads as "no creatures".
    std::vector<uint32_t> worstLevelAtSample;
};

struct RouteLimits
{
    // THE SAME `??` RULE THE DESTINATION GATE USES, and named the same way so
    // the two cannot drift: ground holding something this many levels above the
    // character is ground the game's own con-colour maths would draw as unknown
    // rather than as a number. See CON_COLOR_UNKNOWN_LEVEL_DIFF.
    uint32_t unknownLevelDiff{10};
    // HOW MUCH UNBROKEN `??` GROUND A WALK MAY CROSS. It is the one free number
    // here, so it is measured rather than chosen. Corridors sampled against the
    // dev realm's own world data on 2026-09-07, 60 yard radius, 30 yard
    // spacing, at the family's real levels:
    //
    //     the walks the family died on
    //       Searing Gorge -> the aim it was walking to   598 yards unbroken
    //       Searing Gorge -> Burning Steppes             444
    //       Burning Steppes -> the same aim              209
    //       the vendor #267 refused, 502 yards off       118
    //     the walks it made without a creature death
    //       the 2,012 yard errand #267 endorsed            0
    //       Kharanos -> the gates of Ironforge             0
    //       Goldshire -> Stormwind, and -> Eastvale        0
    //       Stormwind -> Kharanos, 3,562 yards             0
    //
    // THE GAP BETWEEN THOSE TWO POPULATIONS IS EMPTY, which is the whole reason
    // this number is defensible rather than a guess, and it is asserted rather
    // than only written down: see TheTwoPopulationsDoNotOverlap in
    // tests/test_travel_route.cpp. Anyone re-tuning this by feel should move
    // that test first and find out what it costs.
    //
    // So this sits inside the gap rather than on either edge. Over 120 origins
    // drawn at random from the service spawns of both continents at level 27,
    // two hundred releases one errand outright and moves two others to a
    // destination a median 61 yards farther on; a hundred and fifty releases
    // five. Like ErrandDeathLimits, this is a first reading, and the death
    // table is what says whether it was right.
    float lethalRunYards{200.f};
};

struct RouteVerdict
{
    bool survivable{true};
    float lethalYards{0.f};             // how much of the line is `??` ground
    float longestLethalRunYards{0.f};   // the longest unbroken stretch of it
    uint32_t worstLevel{0};             // the worst thing standing along it
};

// One route, judged. An empty reading is survivable, which is the convention
// the guard fields below already carry and for the same reason: a caller that
// did not measure has not made a claim.
RouteVerdict JudgeRoute(RouteReading const& reading, RouteLimits const& limits);

// One spawn of the wanted role standing on the character's own map. The
// caller has already asked whether this character may interact with it, the
// same way the bank and repair candidate lists arrive already asked.
//
// AND WHETHER IT IS A PLACE THE CHARACTER CAN STAND (#267). `guardCount` is
// how many creatures hostile to this character AND above its level are
// spawned within the threat radius of this spawn, and `guardLevel` is the
// highest level among them. Both are measured by the caller off spawn data
// rather than the live grid, for the reason GRAVEYARD_THREAT_RADIUS gives: a
// destination two grids away is not loaded, and an unloaded grid reads as "no
// creatures", which is exactly the wrong answer for this question.
//
// A caller that has not measured a candidate leaves these zero, which reads as
// unguarded. That is deliberate and it is what lets the measurement be done
// lazily in distance order: an unmeasured candidate is always FARTHER than the
// one chosen, so it could not have won and measuring it would have bought
// nothing but a sweep over every spawn in the world.
struct TravelTargetCandidate
{
    uint32_t entry{0};
    float distance{0.f};      // yards from the character, two-dimensional
    bool mayInteract{false};
    uint32_t guardCount{0};   // hostile spawns above this level within the radius
    uint32_t guardLevel{0};   // the highest level among them, for the log line
    // AND WHETHER THE WALK TO IT CAN BE SURVIVED. False only where a caller has
    // read the route and JudgeRoute refused it. An unmeasured candidate is
    // survivable for exactly the reason an unmeasured one is unguarded, which is
    // what lets the reading be taken on a shortlist in distance order rather
    // than on every spawn of the role on the map. The two numbers beside it are
    // for the log line and stay zero when nothing was measured.
    bool routeSurvivable{true};
    float routeRunYards{0.f};
    uint32_t routeLevel{0};
};

enum class TravelTargetVerdict : uint8_t
{
    Chosen,               // `index` names the spawn to walk to
    NothingOfThatKind,    // no spawn of the role is on this map at all
    NoneWillDealWithUs,   // there are spawns and this character may use none
    EveryOneIsGuarded,    // it may use some, and every one stands in hostile ground
    // it may use some, none of those is guarded, and the ground on the way to
    // every one of them is above this character (#267's out-of-scope half)
    EveryRouteIsLethal,
};

struct TravelTargetChoice
{
    TravelTargetVerdict verdict{TravelTargetVerdict::NothingOfThatKind};
    int index{-1};          // into the candidate list, -1 when nothing was chosen
    int nearestRefused{-1}; // the nearest one it may NOT use, for the log line
    std::size_t considered{0};
    std::size_t refused{0};
    int nearestGuarded{-1}; // the nearest usable one standing in hostile ground
    std::size_t guarded{0}; // how many usable ones were refused for their guards
    int nearestLethalRoute{-1}; // the nearest one the walk to was refused
    std::size_t lethalRoutes{0}; // how many were refused for the ground on the way
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
//
// AND A SPAWN IT CAN USE BUT CANNOT REACH ALIVE IS NOT AN ANSWER EITHER
// (#267). A second gate, for the same reason and on the same terms as the
// first: a candidate with hostile spawns above this character's level standing
// within the threat radius of it is not ranked lower, it is not a candidate.
// Measured live, an Alliance party of 25 to 31 was sent to a faction 35 vendor
// it could trade with perfectly well, standing 21 yards from eight level 65
// elites, and died there eighteen times in sixteen minutes. The interaction
// gate above cannot see that: the counter was willing, the ground was not.
//
// THIS IS THE TEST THE GRAVEYARD PATH ALREADY MAKES, and the whole argument
// for the shape is that the module was making two different answers to one
// question. GraveyardRefusal will not RESURRECT a character where hostile
// spawns above its level sit within GRAVEYARD_THREAT_RADIUS, and nothing
// stopped the same module WALKING it to such a place on an errand. Now the two
// refusals sit side by side and neither can be changed without the other being
// read.
//
// THE ORDER OF THE TWO REFUSALS IS PART OF THE MEANING. Interaction is asked
// first because it is a property of the counter and needs no sweep; the guard
// test is asked only of candidates that survived it, so the expensive question
// is never asked about a spawn that was already out. And when nothing at all
// can be chosen, EveryOneIsGuarded wins over NoneWillDealWithUs whenever both
// happened, because "there is a shop here you may use and it is lethal" names
// a danger and a fix, while "nobody will serve you" names neither.
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


// -------------------------------------------- who the kill hooks named --
//
// WHY A KILLER TYPED 'player' CARRYING THE VICTIM'S OWN NAME IS NOT A KILLER
// (#249).
//
// THE ROW THIS EXISTS TO STOP LYING. `overseer_death` fills its killer
// columns from the two script hooks the core offers, and the comment beside
// that code used to say an absent hook meant "fall damage, drowning, fatigue,
// lava, a GM command", with 'environment' as the honest answer for the whole
// class. THAT IS THE WRONG WAY ROUND, and it is wrong in the pinned core, at
// two lines that can be read:
//
//   * Player::EnvironmentalDamage (Player.cpp:853) deals its damage with
//     `Unit::DealDamage(this, this, ...)`. Attacker and victim are the same
//     Player, for every environmental type there is: a fall, drowning,
//     fatigue, fire, lava, slime, and the out-of-bounds kill.
//   * Unit::Kill's hook block (Unit.cpp:14298-14306) then asks only whether
//     the killer is a Player and whether the victim is a Player. It has NO
//     `killer != victim` guard - unlike the KILLED_BY_PLAYER achievement two
//     lines above it, which does have one. So it fires
//     OnPlayerPVPKill(victim, victim).
//
// So an environmental death does NOT arrive with an absent killer. It arrives
// through the player-kill hook naming the victim, and the row it writes is
// `killer_type='player'`, `killer_entry=0`, `killer_name` = the character's
// own name. On 2026-09-06 that row was read as "attributed to himself, so not
// environmental" TWICE in one day, and the second reading survived long enough
// to declare a fall bug fixed while the core's own falling counter was still
// climbing. A column that means "no attributable killer" must not be spelled
// the same way as one that means "another player did it".
//
// WHAT THIS DOES AND DOES NOT CLAIM. It separates the two, and no more. The
// hook cannot say WHICH environmental type it was - EnvironmentalDamage keeps
// that in a parameter it does not pass on - so SelfInflicted is exactly as far
// as the evidence goes, and guessing 'falling' from a position would be the
// same mistake in the other direction. `criteria 149-153` on the character is
// where the type actually lives, and it is only written to the database when
// the player is SAVED, so a read of it that has not been forced with a save
// first is stale by up to PlayerSaveInterval.
enum class KillerKind
{
    Unattributed,   // no kill hook fired: nothing named a killer at all
    Creature,       // a creature landed the killing blow
    Player,         // ANOTHER player did
    SelfInflicted,  // the hook named the victim itself: self-damage
};

// The value written to `overseer_death.killer_type`. Unattributed stays
// 'environment' because that is what the column has always held for it and
// what every existing reader groups by; the new value is the one that used to
// hide inside 'player'.
char const* KillerKindName(KillerKind kind);

// `hookFired` is false when neither kill hook left anything behind for this
// death. `hookType` is what the hook itself said - 'creature' or 'player' -
// and is ignored when hookFired is false. Names are compared without regard to
// case, because the two sides reach this from different places.
KillerKind NameTheKiller(bool hookFired, std::string const& hookType,
                         std::string const& killerName,
                         std::string const& victimName);

// THE FALL BASELINE, AND WHY IT HAS TO BE PUT BACK UNDER A CHARACTER'S FEET.
//
// The core remembers where a character's current fall began and charges
// `m_lastFallZ - landingZ` on the next MSG_MOVE_FALL_LAND
// (`Player::HandleFall`). Two things keep that number honest, and NEITHER of
// them works for this roster.
//
// FIRST, A TELEPORT SETS IT TO THE DESTINATION. `Player::TeleportTo`'s near
// branch ends with `SetFallInformation(GameTime::GetGameTime().count(), z)`
// where `z` is where the character is going (Player.cpp:1532), and the
// client's teleport ack writes it again (MovementHandler.cpp:321). That is
// truthful at the instant it happens and stops being truthful the moment the
// character walks away from there. THIS MODULE'S LIFT IS ONE SUCH TELEPORT,
// and by construction its destination is the whole gap that triggered the
// recovery plus the clearance above where the character stood: at least ten
// and a half yards, bounded only by the surface probe.
//
// SECOND, `Player::UpdateFallInformationIfNeed` WALKS IT BACK DOWN as the
// character moves. It runs on a movement packet from a client and on nothing
// else. THIS ROSTER IS MOVED BY SERVER-SIDE SPLINES - every death row of this
// shape carries movement_generator 'point' - and a spline relocates a
// character without any packet at all. So whatever height was last written
// stays written for as long as the character is being driven, and
// `HandleFall` runs BEFORE `UpdateFallInformationIfNeed` in the same handler
// (MovementHandler.cpp:634 against :685), so a stale figure is spent before
// anything corrects it.
//
// MEASURED, 2026-09-06, AND THE MEASUREMENT IS WHY THIS IS NOT ONLY ABOUT THE
// LIFT. Four characters died of falls at full health while standing still:
//
//   19:15:22  Bork  died z 65.7   lifted to z 157.3 at 19:11:53
//                   157.3 - 65.7 = 91.6 yards = 1.41x max health. Exact.
//   20:10:13  Ugga  died z 93.37  NEVER LIFTED AT ALL
//   20:10:13  Grog  died z 93.47  lifted to z 158.8, which is 52 hp SHORT
//   20:10:40  Grug  died z 91.67  lifted to z 149.1, which is 297 hp SHORT
//
// The last three each need a baseline near z 162.4 to have been killed, and
// the party's travel aim at that moment was z 162.425 - a match to within a
// single hit point for the tightest of them, across three characters with
// three different health pools. One had no lift behind it at all, and the two
// that did were lifted to heights that could not have done it.
//
// SO THE LIFT IS ONE WAY TO LEAVE A STALE HEIGHT IN THERE AND NOT THE ONLY
// ONE. A guard that watched only the lift would have prevented exactly one of
// those four deaths. What the four have in common is not what wrote the
// height. It is that the character was STANDING when the core charged it.
//
// THE RULE IS THEREFORE AN INVARIANT AND NOT AN EPISODE: a character that is
// not falling is standing somewhere, and a character that is standing
// somewhere owes nothing for having got there. Whenever this module is
// entitled to an opinion about a character, it puts the baseline back under
// that character's own feet.
//
// AND IT CANNOT SWALLOW A REAL FALL. The only exemption it needs is `falling`,
// and one poll is enough to catch every fall that could ever be charged for.
// The core's gravity is 19.29110527 (Movement/Spline/MovementUtil.cpp:24), so
// a body in free fall covers 0.5 * g * t^2 = 9.65 yards in its first second,
// and the core charges nothing for a drop under 13.48 yards at all
// (MIN_FALL_DMG_DIST, Player.cpp:14175). Falling far enough to be charged for
// takes just over 1.18 seconds, so at the caller's one-second poll no
// chargeable fall can pass between two polls unseen: every one of them is met
// with `falling` true at least once. A fall short enough to hide between two
// polls is a fall the core would have priced at nothing.
//
// THAT MAKES THE CALLER'S POLL CADENCE PART OF THIS RULE. A poll slower than
// 1.18 seconds could let a chargeable fall through, and that would be a change
// to this decision and not only to a timer.
// NO POSITION AT ALL, as a number no map can produce.
//
// The rule below measures a fall from two positions one second apart, so a
// STALE pair is the one input that can make it lie, and it would lie in the
// direction of inventing a fall and standing the guard down - which is the
// exact failure the guard exists to fix. `seen` already gates that, and this
// is the belt to its braces: when the guard has no opinion it forgets WHERE
// the character was as well as THAT it knew, so a reader who someday trusts
// `lastSeenZ` without checking `seen` gets an absurd answer rather than a
// plausible one.
//
// ABSURDLY LOW RATHER THAN ZERO, and the direction is the point. Measured
// against this, any real position reads as an enormous CLIMB, which is not a
// fall, so the guard runs. A stale high reference would read as an enormous
// drop, which is a fall, so the guard would stand down. Only one of those two
// mistakes is safe, and this is it. Zero, which is a plausible height in most
// maps, is neither.
constexpr float FALL_BASELINE_NO_POSITION = -1000000.0f;

struct FallBaselineState
{
    // Whether this module has ever put a height under this character, and
    // which. NOT a gate on the rule below - the rule holds for every character
    // this module may inspect, lifted or not, which is the whole lesson of the
    // 20:10:13 pair. It is here so the one height this module KNOWS it chose
    // can be named by a reader and asserted by a test.
    bool held{false};
    float z{0.f};
    time_t at{0};

    // WHERE THIS CHARACTER STOOD ON THE PREVIOUS POLL, which is how the rule
    // below tells a fall from a walk without asking a flag. Updated on every
    // poll that has a position, including the polls that decline to rebase,
    // because a fall spanning several polls has to be measurable across all of
    // them rather than only the first.
    bool seen{false};
    float lastSeenZ{FALL_BASELINE_NO_POSITION};
    time_t seenAt{0};
};

// HOW FAST A CHARACTER HAS TO BE LOSING HEIGHT before this module treats it as
// a fall in progress rather than a walk downhill.
//
// DERIVED, NOT PICKED. The core's gravity is 19.29110527
// (Movement/Spline/MovementUtil.cpp:24), so a body in free fall covers
// 0.5 * g * t^2 = 9.65 yards in its first second and more in every second
// after. A character on its feet is bounded by its run speed, 7 yards per
// second, times the sine of the steepest slope the navmesh will let it walk,
// which puts its vertical rate under 5.4. Seven yards per second sits between
// the two with margin on both sides.
//
// WHAT IT COSTS AT THE EDGES, said plainly. A mount at speed down a steep hill
// can exceed seven, and this will read that as a fall and leave the baseline
// alone for those polls; the guard resumes the moment the character slows,
// so the cost is coverage rather than correctness. And a fall caught in its
// first fraction of a second is below the rate, so the baseline is rebased
// once at the very top of it: the charge that follows is short by that
// fraction of a yard and by nothing else.
struct FallBaselineLimits
{
    float fallingYardsPerSecond{0.f};
};

struct FallBaselineVerdict
{
    // Hand the core a fall baseline of `z` for this character.
    bool rebase{false};
    float z{0.f};
};

// THIS MODULE HAS JUST PUT A CHARACTER AT `z`, which for now means the lift.
// Recording it changes nothing about what the step below decides; it is the
// module writing down the one height it is certain it is answerable for.
void FallBaselineHandedOver(FallBaselineState& state, float z, time_t now);

// THE STATES THIS MODULE HAS NO OPINION ABOUT AT ALL, which is
// TerrainRecoveryMayInspect's list with the two untrustworthy flags removed.
//
// It is a separate predicate from TerrainRecoveryMayInspect rather than a
// reuse of it, and the difference is the whole of #291: the RECOVERY may
// reasonably decline to move a character whose flags say flying or falling,
// because moving one is expensive and being wrong about it is worse. The
// GUARD only ever writes down where the character already is, so the same
// caution buys it nothing and costs it everything.
bool FallBaselineMayInspect(bool alive, bool teleporting, bool inFlight,
                            bool inWater, bool onTransport, bool inVehicle);

// ONE POLL, FOR ONE CHARACTER.
//
// WHY THIS STOPPED ASKING WHETHER THE CHARACTER IS FALLING (#291). It used to,
// and the instrument added in #281 caught it: on every phantom death sampled,
// the stand-down mask was exactly 16, FALL_GUARD_FALLING, with an age of 0 or
// 1 second. The guard was reached every single poll and declined every single
// poll, on characters at full health whose measured descent was 1.63 and 1.88
// yards. The flag was on while the character stood still.
//
// It is not merely never cleared, it is RE-SET, and that is measured rather
// than assumed: `Player::TeleportTo` reduces the movement flags to
// MOVEMENTFLAG_MASK_HAS_PLAYER_STATUS_OPCODE (UnitDefines.h:423), which drops
// FALLING, so every graveyard revival clears it - and the recovery drive,
// which cannot run at all while IsFalling is true, was observed running twice
// between two masked deaths at 01:41:44 and 01:41:53. Clear then, set again by
// the next death. A rule that clears the flag once would therefore fix
// nothing.
//
// SO THE GUARD MEASURES THE FALL INSTEAD OF ASKING ABOUT IT. `Unit::IsFalling`
// is `HasMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR)` or a
// falling spline (Unit.cpp:15957), and on a roster driven by server-side
// splines nothing clears the first of those: `EffectMovementGenerator::Finalize`,
// the generator MoveFall installs, opens with `if (!unit->IsCreature()) return;`.
// On a normal realm the client's own movement packets clear it. These send
// none. A number this module takes itself, from two positions one second
// apart, does not have that problem.
//
// AND THE OTHER STAND-DOWN IS NOW THE CORE'S OWN GATE. `coreWouldNotCharge` is
// `HasHoverAura() || HasFeatherFallAura() || HasFlyAura()`, which is exactly
// what `Player::HandleFall` consults before charging (Player.cpp:14187-14189).
// The core does not refuse to charge because a flag says flying; it refuses on
// those three auras. Asking the same question the charging gate asks is what
// closes the hole, and it is narrower than what was there before rather than
// wider: nothing that used to be charged stops being charged.
//
// NOTHING IS WEAKENED. A real fall is still left alone, because a real fall is
// still losing height faster than a walk can and this declines on that
// measurement. The flag is not consulted in either direction, which also
// covers the case the flag is wrongly CLEAR during a real descent - and there
// is a known path that clears it under a live MoveFall spline (#254), so that
// is not hypothetical either.
//
// `standingZ` is where the SERVER believes this character's feet are, which is
// the right number whichever way the server and the client disagree: if a
// spline has walked the character down, that is the honest new baseline, and
// if a silent client has fallen without the server hearing about it, the
// server's stale higher figure is the honest OLD baseline and handing it back
// changes nothing.
FallBaselineVerdict FallBaselineStep(FallBaselineState& state, bool mayInspect,
                                     bool coreWouldNotCharge, float standingZ,
                                     time_t now, FallBaselineLimits const& limits);

// ------------------------------------------------ the addon language (#269) --

// How a `kind='chat'` row addressed to a group is put on the wire.
//
// WHY THIS IS A DECISION AND NOT AN `if` AT THE CALL SITE. Two different
// things travel over party chat and they want opposite treatment. One is
// speech: a council answer, a trade, a crafting request, every word of it
// written to be read by whoever is watching that character. The other is a
// status push - a tab separated line addressed to an addon, which a person
// reads as noise and which the sender has to keep out of the chat frames by
// hand. The row already says which it is, in the only field that describes how
// a line should travel, so that is where the answer is read from.
//
// 3.3.5a separates the two itself, and not by convention. LANG_ADDON on a
// group channel is the transport every addon's SendAddonMessage uses: the
// receiving client hands the packet to CHAT_MSG_ADDON and no chat frame is
// ever asked to draw it. The core relies on this for its own addon channel
// command replies, which would otherwise be printing into players' whisper
// windows. So `party` is speech and `party_addon` is the same packet, to the
// same recipients, in the language nothing renders.
//
// THE TOKENS ARE LISTED, NOT PARSED. Stripping an `_addon` suffix would also
// accept `_addon` on its own and would quietly read `party_addonx` as party
// chat, and a channel this function does not recognise has to stay
// unrecognised: the caller rejects it by name, which is what makes a module
// too old to know these tokens mark the row an error rather than deliver
// machine text as speech.
struct GroupChatRoute
{
    // Is this token a group channel at all? False sends the caller on to its
    // other branches and finally to "unknown chat channel".
    bool group = false;
    // Raid rather than party. Decides the chat type on the packet and, inside
    // a raid, who a party line reaches. Nothing else.
    bool raid = false;
    // Send it in the addon language. Delivered to addons, drawn by nothing,
    // and therefore not speech - so it is not captured for the watchers
    // either.
    bool addon = false;
};

GroupChatRoute GroupChatRouteFor(std::string const& channel);

// ------------------------------------ an errand that is killing its traveller --
//
// THE RULE AGENTS.md ALREADY STATES, AND WHICH NOTHING IMPLEMENTED. "Aim the
// party leader only, and watch the death table while it walks. If deaths
// exceed roughly three in five minutes, clear the aim - the destination is not
// worth the crossing." That was written from #78, where a level 17 party was
// routed through a level 20-30 zone and then into a level 50-58 one and the
// deaths went from a six-hour quiet streak to 24 in fifteen minutes.
//
// It has been an instruction to whoever happened to be watching ever since,
// and on 2026-09-06 nobody was. Measured on `overseer_death`: an Alliance
// family of five, levels 25 to 31, died 46 times in 36 minutes inside one
// hostile camp in zone 17, 43 of those to a single level 65 elite, with the
// leader's `travel_npc` reading the same errand at every one of them. The
// aimed leader ALONE died 13 times, four of them inside one four-minute
// stretch. Nothing in the module counted.
//
// WHAT DID EXIST, AND WHY IT WAS NOT THIS. There is already one death-rate
// rule - the stuck-revival trap, three deaths within 100 yards in 15 minutes -
// and its condition was met over and over that evening. It answers a different
// question: not "should this errand still be running" but "where should this
// character come back to life". Its only remedy is a teleport to the leader's
// bind point, and a revival may not change maps while a graveyard exists on
// this one (#241). Every character on that roster binds to map 0 and the party
// was on map 1, so the remedy was structurally out of reach for the whole
// window: the trap was detected every time and fed every time. A breaker whose
// one lever is held down by another rule is not a breaker.
//
// SO THIS ONE PULLS THE OTHER LEVER - the errand, which is the fuel. It does
// not ask WHY the destination is lethal, and that is the point of having it as
// well as a danger gate on the resolve (#267): a gate can only refuse what it
// can see standing there when it looks. Bodies are the one measurement that
// needs no theory of the danger - a patrol that wandered in, a route that
// kills, a camp that grew, a level gap nothing sampled, or an aim written into
// the column by a hand outside this module entirely.
struct ErrandDeathLimits
{
    // Three in five minutes, from AGENTS.md, deliberately the same numbers a
    // person was being asked to apply by eye. Nothing is gained by inventing
    // better ones before this has ever run.
    uint32_t deaths{3};
    int64_t windowSeconds{5 * 60};
    // HOW LONG A RELEASED TARGET STAYS REFUSED, WHICH IS NOT DECORATION. This
    // module is not the only writer of `overseer_roster.travel_npc`; the
    // deployment's own bridge writes it too. Measured on 2026-09-06: the aimed
    // column was observed empty at 20:55 and was carrying the same errand again
    // by 21:00. A release with no memory is therefore a mechanism that reports
    // itself, changes nothing for longer than one poll, and sends the family
    // back to the thing that killed them - which is worse than no breaker,
    // because the log now says the breaker fired.
    //
    // Bounded rather than permanent: a vendor the family genuinely needs is
    // worth trying again once whatever killed them has had time to be
    // somewhere else, and nothing here can tell a camp from a patrol.
    int64_t cooloffSeconds{15 * 60};
};

// How much of the death table this errand is answerable for, in seconds. An
// errand younger than the window is judged over its OWN life and not one
// second longer, for the same reason TravelAimBook::Release erases the errand
// memory it ends: an aim that inherits the previous errand's corpses is
// released before it has walked a yard. Zero for an errand with no age yet,
// which the caller reads as "there is nothing to ask the table".
int64_t ErrandDeathWindow(int64_t errandSeconds, ErrandDeathLimits const& limits);

struct ErrandDeathToll
{
    // Deaths this TRAVELLER suffered inside ErrandDeathWindow. Its own, not
    // the party's: only a character that carries `new rpg` can be sent
    // anywhere, the followers arrive by following, and a follower's death is
    // not evidence about a destination it was never sent to. Measured
    // sufficient above - the aimed leader's own count crosses this threshold
    // well before the family's does. Whether a leader that survives while its
    // followers are farmed should also count is a real question and is not
    // this one.
    uint32_t deaths{0};
    // Seconds since this character was last refused THIS target by this rule,
    // or -1 when it never was.
    int64_t sinceRefused{-1};
    // SOMETHING OTHER THAN THIS DRIVE ISSUED THIS AIM AND RE-ISSUES IT.
    // TravelAimBook::Claim is the one door into the column that is not
    // DriveTravel's own, so "claimed" is exactly this fact - and it is the one
    // thing an escort check cannot supply, because a leader on a staging aim is
    // not escorted, he is aimed.
    //
    // It matters because a release here would be UNDONE within one poll of
    // whatever holds the claim, and would reset the errand's pin and backstop
    // clock every time while it lasted. Firing into that is not a breaker
    // either, so a claim something answers for is declined and said out loud
    // instead. A run that keeps killing its party is the run's own stall to
    // answer, and it can be LEFT to answer it because every aim a run claims
    // sits behind a timer that ends the run: DUNGEON_STAGING_BACKSTOP_SECONDS
    // over RESETTING, GATHERING and BARRIER, DUNGEON_CROSSING_BACKSTOP_SECONDS
    // over ENTER and EXIT, and CROSSING_BACKSTOP_SECONDS over a continent
    // crossing - which exists BECAUSE of this deference and says so.
    //
    // THE NAME IS THE OLDER HALF OF THE TRUTH. The column fact it reads has not
    // changed; what changed is that a dungeon run stopped being the only thing
    // that writes it. See `catchUp`.
    bool runOwned{false};

    // ...AND THE CLAIMANT IS THE CATCH-UP WALK, WHICH NOTHING ANSWERS FOR
    // (#298). The paragraph above was true when it was written and #138 made it
    // false: the follow drive walks a follower too far back to be following to
    // its leader through the same lease a run uses, so that walk reads as
    // run-owned here and is not a run.
    //
    // NOTHING ENDS IT. Its own end conditions are arriving and the party
    // splitting across maps, which are both things going right; its sweep only
    // fires when the party poll stops marking it, which is a leak guard and not
    // a clock; and the errand's own twenty-minute unreachable backstop is
    // restarted by every re-aim at a leader who has walked on, and is re-claimed
    // by the next party poll on the occasions it does fire.
    //
    // MEASURED ON THE DEV REALM, 2026-09-07: a follower died three times in 153
    // seconds on one catch-up aim, this rule fired, and it deferred to a dungeon
    // run whose newest row had ended thirteen hours earlier with
    // outcome='staging_failed'. The deference is right and its coverage was
    // wrong. A claim nothing answers for is answered here instead.
    bool catchUp{false};
};

enum class ErrandDeathRemedy
{
    Continue,         // nothing to answer
    Release,          // the destination is not worth the crossing
    RefuseReissue,    // released already, and something has re-aimed it since
    DeclineRunOwned,  // it would fire, and a release here would be inert
    EndCatchUp,       // it would fire, and the walk that re-aims it is the only
                      // thing that can stop it
};

struct ErrandDeathVerdict
{
    ErrandDeathRemedy remedy{ErrandDeathRemedy::Continue};
    // Seconds of cool-off still to run. Meaningful for RefuseReissue only, and
    // zero everywhere else.
    int64_t coolOffRemaining{0};
};

// ONE POLL, FOR ONE OUTSTANDING ERRAND.
ErrandDeathVerdict ErrandDeathBreaker(ErrandDeathToll const& toll,
                                      ErrandDeathLimits const& limits);

// ---------------------------------- an errand that is eating the questing --
//
// THE SECOND WAY AN ERRAND GOES WRONG, AND THE ONE THE BREAKER ABOVE CANNOT
// SEE. That breaker asks whether an errand is killing its traveller. This one
// asks whether an errand that is going perfectly well is nonetheless the whole
// of what a character does.
//
// MEASURED ON THE DEV REALM, 2026-09-08, over thirty minutes of one character's
// world log. Nine economy errands, alternating `vendor` and `repair`, holding
// the quest drive down for 60, 280, 60, 81, 60, 60, 60, 60 and 460 seconds:
// 1181 seconds of 1781, or 66.3% of the wall clock. Every one of them ARRIVED
// and released cleanly - ten arrivals for ten sendings, and the four `repair`
// rows that ran in the same day all answered "repaired". Nothing failed.
// Meanwhile that character's `quests_rewarded` had not moved in twelve hours
// while his four siblings, who were issued no travel errands at all in the same
// window, gained levels.
//
// WHY NOTHING ALREADY CATCHES IT. Every existing guard on this drive is keyed
// to an errand going BADLY - deaths, an unreachable destination, refused
// footing, a twenty-minute backstop. A short errand that arrives is invisible
// to all of them, and the harm here is not in any single errand; it is in the
// rate. Nine correct errands in half an hour is the bug, and no one of the nine
// is.
//
// WHY IT IS NOT SELF-LIMITING. The errand column is written by a bridge outside
// this module, which re-arms it on a fixed cadence for as long as its reason
// holds - and arriving at the counter is not what clears that reason, so the
// reason holds forever. Measured over eight hours the same character was
// nominated for the economy errand 181 times out of 181 and no sibling once.
// A character that is near a counter is therefore kept near it.
//
// SO THE RULE IS A BUDGET AND NOT A COOLDOWN, and that choice is forced by the
// measurement rather than preferred. The observed loop ALTERNATES roles - five
// `vendor` against three `repair` in the window above - so a rule of the shape
// "not the same errand twice in a row" never fires on it even once. A budget
// does not ask which counter it is walking to; it asks what share of a
// character's life the counters have had, which is the thing that was measured
// and the thing that hurts.
//
// AND IT REFILLS, which is what keeps it from being a tax on characters that do
// not have this problem. A character that has been questing has a full budget
// and its first errand goes out with no delay whatever. Only one that has
// already spent its share is held off, so the four siblings above would never
// notice this rule exists.
struct ErrandBudgetLimits
{
    // The window the share is measured over. Half an hour, because that is the
    // order of a quest: long enough that one legitimate town trip inside it is
    // not remarkable, short enough that a character does not have to be wrong
    // for an hour before anything answers.
    int64_t windowSeconds{30 * 60};

    // How much of that window economy errands may hold. Seven minutes in
    // thirty, a little under a quarter.
    //
    // NOT TUNED TO THE MEASUREMENT, DELIBERATELY. The observed 66.3% could be
    // met by any number below it, and picking one just under the observed value
    // would encode this evening's loop rather than a rule. A quarter is the
    // share at which errands are plainly still the minority of what a character
    // does, which is the property actually wanted, and it is three times what
    // the untroubled siblings spent (zero).
    int64_t spendSeconds{7 * 60};
};

// What economy errands have cost this character lately, as a draining total.
//
// A BUCKET RATHER THAN A LIST OF TIMESTAMPS. The question is only ever "how
// much, lately", so the answer is one integer and a clock, and the memory is
// bounded at the size of the roster forever - the same reason the refusal
// memory beside it keeps one entry per character and not a growing list.
struct ErrandSpend
{
    // Errand seconds still counted against this character.
    int64_t seconds{0};
    // When `seconds` was last brought up to date. Zero means never, which is a
    // character this rule has not yet had to think about rather than one with a
    // spend of zero at the epoch.
    time_t markedAt{0};
};

// Bring a spend up to `now` and add `heldSeconds` of fresh errand to it.
//
// THE DRAIN IS THE RULE, not a decoration on it. The bucket loses
// `spendSeconds` for every `windowSeconds` of wall clock that passes, so a
// character spending exactly its allowance holds level, one spending less
// drains to empty and one spending more fills up and is eventually refused.
// That is the whole of "a share of a window" expressed without keeping a
// window's worth of history.
//
// CLAMPED AT BOTH ENDS, AND NOT AT THE SAME PLACE. Never below zero, because
// credit banked by a character that has not been near a counter for a day is
// not a licence to spend a day at one. Above the line it is allowed to reach
// TWICE the budget and no further: the rule is asked once per poll, so an
// errand still running when the budget is reached costs the rest of that poll,
// and a total that cannot go past the line would discard that overshoot rather
// than repay it - handing the same seconds back on every saturation. Measured
// over a six-hour replay of the observed loop, clamping at the line leaked 600
// seconds and took a 23.3% allowance out at 26.1%. Bounded at twice rather than
// unbounded because from there exactly one window of drain brings it back to the
// line, so a single bad stretch cannot cost an afternoon of refusing errands
// that are now perfectly reasonable.
ErrandSpend ErrandSpendAfter(ErrandSpend const& before, time_t now, int64_t heldSeconds,
                             ErrandBudgetLimits const& limits);

// Has this character had its share? `>=` and not `>`, for the same reason the
// death rule uses it: the second that reaches the line is the evidence, and a
// rule that waits to be sure spends the thing it is protecting.
bool ErrandOverspent(ErrandSpend const& spend, ErrandBudgetLimits const& limits);

// ----------------------------------------------------------------- auction --
//
// THE PARTS OF kind='auction' THAT NEED NO WORLD: reading the command text,
// the three-value duration rule, the bid rule as the core applies it, what a
// bid actually costs, and whether a refusal is worth trying again unchanged.
//
// WHY THESE ARE HERE AND NOT NEXT TO DoAuction. The executor in
// mod_overseer.cpp drives WorldSession::HandleAuctionSellItem /
// HandleAuctionPlaceBid / HandleAuctionRemoveItem, and every one of those
// answers the CLIENT - a status packet on the session, void return - so the
// module has to test each refusal itself before the call in order to name it.
// Most of those tests are questions about the world (is there an auctioneer
// in reach, is the item soulbound, does the house hold that id). The ones
// below are not: they are arithmetic on numbers the caller has already read,
// and a rule like "is 105 copper enough over a 100 copper bid" that nothing
// can exercise without a running worldserver is a rule nobody will check.
//
// THE GRAMMAR, one verb per row, `target_arg` unused:
//
//   list guid:<item_instance.guid> bid:<copper> buyout:<copper> hours:<12|24|48>
//   buy auction:<auctionhouse.id>
//   bid auction:<auctionhouse.id> bid:<copper>
//   cancel auction:<auctionhouse.id>
//
// `key:value` pairs after the verb, in any order, each at most once. Every
// value is a decimal copper amount or an id; there is no gold/silver notation
// because the Python side already speaks copper (it reads `auctionhouse` and
// `item_instance` directly for browsing, so it never needs the executor to
// translate). `buyout:0` means no buyout, which is what the core means by it
// (AuctionHouseHandler.cpp:492, `auction->buyout == 0`).

enum class AuctionVerb
{
    None,    // did not parse; `error` says why
    List,
    Buy,
    Bid,
    Cancel,
};

struct AuctionRequest
{
    AuctionVerb verb{AuctionVerb::None};
    uint32_t itemGuid{0};    // list: the carried item_instance guid
    uint32_t auctionId{0};   // buy, bid, cancel: the auctionhouse.id
    uint32_t bid{0};         // list: the starting bid; bid: the bid placed
    uint32_t buyout{0};      // list: 0 = no buyout
    uint32_t hours{0};       // list: 12, 24 or 48
    // Empty when it parsed; otherwise one of the AuctionRefusal literals below,
    // which have static storage so the executor can hand it on as `detail`
    // without copying.
    char const* error{""};
};

AuctionRequest ParseAuctionRequest(std::string const& command);

// The listing lengths the core accepts, as the packet carries them. The sell
// handler reads `etime` in MINUTES, multiplies by MINUTE and then accepts
// exactly 1x, 2x and 4x MIN_AUCTION_TIME, which is 12 hours
// (AuctionHouseHandler.cpp:156-193, AuctionHouseMgr.h:34); anything else
// returns without a word. So hours:12/24/48 become 720/1440/2880, and every
// other hour count is 0 here and a named refusal in the row.
uint32_t AuctionDurationMinutes(uint32_t hours);

// THE BID RULE, in the order HandleAuctionPlaceBid applies it
// (AuctionHouseHandler.cpp:487-497). The core says nothing on any of these -
// it simply returns - so this is the only place a bot learns which one it hit.
//
//   NotAboveCurrent  price <= the standing bid, or below the starting bid
//   BelowIncrement   not a buyout, and short of bid + the core's outbid step
//   Ok               the core will take it (money permitting)
//
// `outbidStep` is the core's own AuctionEntry::CalculateAuctionOutBid(bid)
// (AuctionHouseMgr.cpp:580-584: 5% of the bid, or 1 copper), passed in rather
// than recomputed here so this file does not carry a second copy of a rule
// the core owns. A buyout (price >= buyout, buyout != 0) skips the increment
// test, exactly as the handler does.
enum class AuctionBidVerdict
{
    Ok,
    NotAboveCurrent,
    BelowIncrement,
};

AuctionBidVerdict AuctionBidAcceptable(uint32_t price, uint32_t startBid,
                                       uint32_t currentBid, uint32_t buyout,
                                       uint32_t outbidStep);

// WHAT A BID OR BUYOUT COSTS THE BIDDER, which is not always the price: a
// bidder raising their own standing bid pays only the difference
// (AuctionHouseHandler.cpp:512-513 for a bid, :553-554 for a buyout). The
// module checks HasEnoughMoney against this figure, not against the price, so
// a character topping up their own bid is not refused for money they are not
// about to spend.
uint32_t AuctionBidCost(uint32_t price, uint32_t currentBid, bool alreadyTopBidder);

// THE REFUSAL LITERALS, in one place. Each is written into `detail` and into
// result.reason by DoAuction, and AuctionRefusalRetryable below is keyed on
// them, so a literal that drifts between the two sides would silently turn a
// permanent refusal into a retried one. Keeping them here is what keeps the
// classification honest, and it is why the test exercises them by these
// names rather than by retyped strings.
namespace AuctionRefusal
{
constexpr char const* Malformed          = "malformed auction command";
constexpr char const* InvalidDuration    = "invalid duration (want hours:12, 24 or 48)";
constexpr char const* NoStartBid         = "no starting bid";
constexpr char const* ZeroBid            = "bid is zero";
constexpr char const* BuyoutBelowBid     = "buyout below starting bid";
constexpr char const* PriceTooHigh       = "price above the money cap";
constexpr char const* NotInRange         = "auctioneer not in range";
constexpr char const* NoSession          = "character has no session";
constexpr char const* NoHouse            = "auctioneer belongs to no auction house";
constexpr char const* Dead               = "character is dead";
constexpr char const* InCombat           = "character is in combat";
constexpr char const* Trading            = "character is in a trade";
constexpr char const* InFlight           = "character is on a flight path";
constexpr char const* Stunned            = "character is stunned";
constexpr char const* LoggingOut         = "character is logging out";
constexpr char const* BelowLevel         = "below the auction level requirement";
constexpr char const* ItemNotCarried     = "item not carried";
constexpr char const* ItemSoulbound      = "item is soulbound";
constexpr char const* ItemQuest          = "item is a quest item";
constexpr char const* ItemNotTradable    = "item cannot be traded";
constexpr char const* ItemAlreadyListed  = "item is already in an auction";
constexpr char const* CannotAffordDeposit = "cannot afford deposit";
constexpr char const* CannotAffordBuyout = "cannot afford buyout";
constexpr char const* CannotAffordBid    = "cannot afford bid";
constexpr char const* CannotAffordCut    = "cannot afford the cancel cut";
constexpr char const* BidTooLow          = "bid too low";
constexpr char const* NoBuyout           = "auction has no buyout";
constexpr char const* AuctionNotFound    = "auction not found";
constexpr char const* AuctionItemMissing = "auction item missing from the house";
constexpr char const* WrongHouse         = "wrong auction house";
constexpr char const* OwnAuction         = "own auction";
constexpr char const* NotOwnAuction      = "not own auction";
constexpr char const* CoreRefused        = "the core refused the transaction";
constexpr char const* NotReadBack        = "the transaction did not read back";
}  // namespace AuctionRefusal

// IS THIS REFUSAL WORTH ASKING AGAIN WITHOUT CHANGING THE ROW. True for the
// walls that move on their own - the character is fighting, dead, mid-trade,
// mid-flight, short of money, or not yet standing at the auctioneer the
// Python side is walking it to. False for everything the same row will hit
// again for ever: a malformed command, a soulbound or quest item, an auction
// that is gone or belongs to the wrong person, a bid the arithmetic rejects.
// Written into result.retryable so the sender can tell a "wait" from a "stop"
// without keeping its own list of this module's strings; the give backoff
// (mod-overseer#169) is what happens when a sender cannot tell the two apart.
bool AuctionRefusalRetryable(std::string const& reason);

// ----------------------------------------- a home somebody chose (#274) --
//
// WHAT A kind='bind' ROW MAY SAY.
//
//     here     bind at the innkeeper this character is already standing next to
//
// WHY THIS VERB EXISTS AT ALL. The game already ships a cross-continent
// crossing that needs no boat and no pier: an innkeeper sets your home, and
// from then on the hearthstone takes you there from anywhere, across an ocean,
// with no deck to board. This module can already do the second half - four
// separate revival exits teleport a character to m_homebind* - and it has
// never been able to do the first. Nothing here, and nothing upstream that a
// bot can reach, could ever CHANGE a home. So "go home" has exactly one
// possible destination per character, and nothing this module or its operator
// can do has ever been able to choose it.
//
// WHAT THAT COST, MEASURED. All five members of the family read map 0, area
// 12, in character_homebind, at one doorway in the human starting zone: the
// widest gap between any two of the five binds is 0.68 yards. Two of them are
// not even human, so this is not five characters keeping the home they were
// born with, it is five characters that have only ever had one home between
// them. The dungeon they are asked to run a hundred times is on map 1, and
// their runs stand at 0 of 100. So the one verb that could have reunited a
// party split across two continents would have gathered all five neatly in the
// wrong hemisphere. It is a reunion, and it is the wrong one, and that is why
// this executor had to exist before anything was allowed to send anybody home.
//
// UPSTREAM HAS THE VERB AND IT CANNOT RUN. mod-playerbots ships
// SetHomeAction, registered as the chat command `home`. Its first act is
// `Player* master = GetMaster()`, and four lines later, when the selection did
// not come from an rpg target, `else return false` (SetHomeAction.cpp:19-25).
// A roster character is masterless whenever no client holds the party leader -
// PlayerbotAI::FindNewMaster returns nullptr unless the leader is a real
// player or a selfbot - so that early return is taken every time, before the
// scan for a nearby innkeeper below it is ever reached. The failure is then
// invisible in both directions: PlayerbotAI::HandleCommands erases the command
// whatever ParseChatCommand answered, and TellError returns false without
// sending anything when there is no master to send it to. The row reads
// `delivered`. This is the pattern AGENTS.md already names - a call that
// reports its failure to a client, to a character that has no client - and the
// answer here is the same one the learn path took: do not trust the call, ask
// the world afterwards.
//
// SO THE MODULE OWNS THE VERB. CMSG_BINDER_ACTIVATE goes to the core's own
// WorldSession::HandleBinderActivateOpcode, exactly the way CMSG_AREATRIGGER
// goes to HandleAreaTriggerOpcode, and for the same reason: the handler
// re-checks everything itself. GetNPCIfCanInteractWith rejects a creature that
// is not innkeeper-flagged, not alive, hostile, or further than
// INTERACTION_DISTANCE, so nothing can be bound anywhere a character did not
// walk to and stand beside. The chain from there is entirely server-side -
// SendBindPoint casts spell 3286, Spell::EffectBind calls Player::SetHomebind,
// and SetHomebind writes character_homebind itself - so no part of it is
// waiting on a client that a bot does not have.
//
// WHAT THIS DELIBERATELY IS NOT. It is not a teleport, and it does not move
// anybody one yard. A character can only be bound where it can already stand,
// which means a bind can never itself be the crossing: it converts a crossing
// that has already been made into a permanent one that costs nothing to make
// again. That is the whole prize, and overstating it would be the same mistake
// #275 made about a stop frame.
enum class BindVerb
{
    None,  // not a bind request; `error` says why
    Here,
};

struct BindRequest
{
    BindVerb verb{BindVerb::None};
    std::string error;  // the refusal literal when verb is None, else empty
};

// ONE FORM, AND NO COORDINATES. Whitespace-tolerant, otherwise literal: the
// single lower-case word `here`, or an empty command meaning the same thing.
// There is deliberately no way to name an innkeeper, a map or a position: the
// core decides who is in reach, and a grammar that could ask to be bound
// somewhere the character is not standing would be a grammar for a request the
// handler is always going to refuse.
BindRequest ParseBindRequest(std::string const& command);

// A home, or a place, as it was read off the world at one moment.
//
// `known` IS NOT `zero`. A map id of 0 is Eastern Kingdoms and a coordinate of
// 0 is a real coordinate, so an unread home has to say so in a field of its
// own rather than by being empty - the same distinction the ratchet had to
// make between no reading and a real zero.
struct HomeBind
{
    bool known{false};
    uint32_t mapId{0};
    uint32_t areaId{0};
    float x{0.f};
    float y{0.f};
    float z{0.f};
};

enum class BindOutcome
{
    // The home before, the home after, or where the character was standing
    // could not all be read. Says nothing about whether anything happened,
    // and must never be reported as either outcome.
    Unreadable,
    // The home is somewhere else than it was. The only outcome that is a
    // change, and the only one worth `applied`.
    Moved,
    // The home did not move AND it is already where this character is
    // standing. Nothing happened because nothing needed to; this is a success
    // and it is not a change, which is exactly what `unchanged` is for.
    SameSpot,
    // The home did not move and it is NOT where the character is standing.
    // The call was made and the world did not agree. THE FAILURE THIS WHOLE
    // EXECUTOR EXISTS TO MAKE VISIBLE.
    Unchanged,
};

// "moved", "same-spot", "unchanged", "unreadable". Here rather than in the
// executor so the word a test pins is the word a row carries.
char const* BindOutcomeWord(BindOutcome outcome);

// `standing` is where the character was when the packet went out, because a
// home that did not move is only good news if it was already here. Distance is
// compared in three dimensions: an inn has floors.
BindOutcome BindReadBack(HomeBind const& before, HomeBind const& after,
                         HomeBind const& standing, float sameSpotYards);

// WHICH INNKEEPER, when more than one is in reach. NEAREST, and not the
// cheapest-first rule ChooseRepairer uses, because a bind has no price for a
// reputation discount to act on - every innkeeper sets the same home to the
// same coordinates. Ties break on index so the answer never depends on the
// order a cell sweep happened to produce. Returns -1 for an empty list.
int ChooseInnkeeper(std::vector<float> const& yards);

// Keyed on the `detail` literal the executor returns. Unknown is `Later`, the
// same call the sell and repair tables make: a refusal this table has never
// heard of is more likely a new transient than a new permanent.
TownRetry BindRefusalRetry(std::string const& detail);

// ------------------- a home the campaign's own dungeon can be reached from --
//
// WHAT IS WRONG, READ OUT OF character_homebind RATHER THAN REMEMBERED (#348).
// The family is asked to run Wailing Caverns, which is approached from map 1,
// and three of the five are bound on map 0:
//
//     Bork  map 0 zone 12   Elwynn Forest, Eastern Kingdoms
//     Grug  map 0 zone 12   Elwynn Forest, Eastern Kingdoms
//     Ugga  map 0 zone 12   Elwynn Forest, Eastern Kingdoms
//     Grog  map 1 zone 392  Ratchet, Kalimdor
//     Og    map 1 zone 467  Kalimdor
//
// The hearthstone is the module's reliable way of sending somebody home and it
// resolves TARGET_DEST_HOME, which IS m_homebind*, so for those three "go home"
// means "cross an ocean away from the door". Nothing here can undo that:
// `follow` cannot cross a map, the catch-up walk returns on a cross-map gap,
// and an `at:` aim cannot name a coordinate on another one. The barrier needs
// all five outside the door, and a member that keeps being posted back to
// Elwynn is one it can never count.
//
// AND THE ONE BIND THAT WORKS SAYS WHAT THE RULE IS. Grog's home at
// (-1050, -3665, 24) is exactly the first waypoint of this module's own
// measured corridor to that door, because the corridor was measured starting
// from the one bind that happened to be right. So the question a drive has to
// ask is not "is this home somewhere" but "is this home where the campaign's
// own approach begins".
//
// WHY THE ANSWER IS NOT "THE NEAREST INNKEEPER". Measured from the door at
// (-733.7, -2214.9) against the innkeepers on that map:
//
//     3934  Innkeeper Boorand Plainswind   540 yards   Horde, the Crossroads
//     6791  Innkeeper Wiley               1484 yards   neutral, Ratchet
//     7714  Innkeeper Byula               1657 yards   Horde
//
// This family is Alliance. GetNPCIfCanInteractWith refuses an unfriendly
// creature however close a character stands, so the nearest one can never set
// anybody's home - and walking up to it walks into a Horde town, where one
// member has already died three times to guards. A rule that sorted by distance
// would send characters to be killed at an innkeeper that would then refuse
// them. So the town is named by the DUNGEON, in the portal row beside the door
// and the corridor, and the faction question is answered by the core's own gate
// at the moment of the bind rather than by arithmetic here.
//
// AND IT IS TWO QUESTIONS, NOT ONE. The map is the fatal half - a home on the
// wrong continent cannot be walked back from at all - but a home on the right
// map and a thousand yards of unmeasured ground from the door is a hearth that
// lands nowhere useful either, and the same remedy answers both.
enum class CampaignHome : std::uint8_t
{
    // The home could not be read. Says nothing about whether it suits, and must
    // never be acted on: the same discipline BindReadBack keeps for the three
    // readings it compares.
    Unreadable,
    // This campaign names no town, so nothing was asked and nothing is wrong.
    // The answer for every portal row that carries no home of its own, which is
    // the same "not measured, so not invented" rule the three zeros in an
    // approach corridor already stand for.
    NotAsked,
    // The home is the campaign's own town. Nothing to do.
    Suits,
    // The home is on a different map from the one this dungeon is approached
    // from. The failure this decision exists for.
    OffTheDungeonsMap,
    // The right map, and far enough from the town that hearthing to it lands
    // the character somewhere the module has not measured a way to the door
    // from.
    TooFarFromTheTown,
};

// "unreadable", "not-asked", "suits", "off-the-dungeons-map", "too-far". Here
// rather than in the drive so the word a test pins is the word a log line
// carries.
char const* CampaignHomeName(CampaignHome verdict);

// WHERE A CAMPAIGN KEEPS ITS HOMES, as the adapter reads it off the portal row.
//
// `known` IS NOT `zero`, for the reason HomeBind gives above: map 0 is a real
// map and 0.0 is a real coordinate, so a row that names no town has to say so
// in a field of its own. The adapter sets it from the module's own existing
// StagingPointCheck rather than from a fourth float, so "that is not a place"
// is decided in one place for corridors and for towns alike.
struct CampaignHomeAnchor
{
    bool known{false};
    std::uint32_t mapId{0};
    float x{0.f};
    float y{0.f};
    float z{0.f};
};

// Does this member's home suit the dungeon its campaign runs?
//
// THE ANCHOR IS ASKED ABOUT FIRST, before the home is even looked at: a
// campaign that names no town is not judging anybody, and an unreadable home is
// only worth reporting where there was something to compare it against.
//
// MEASURED IN TWO DIMENSIONS, unlike BindReadBack directly above it, and the
// difference is the question rather than an oversight. That one asks whether a
// character moved between two binds and an inn has floors, so it counts height.
// This one asks whether a home is in the right TOWN, and every distance this
// module measures against a place on a map - the barrier circle, the arrival
// check, the staging watchdog - is a 2D one.
CampaignHome ReadCampaignHome(HomeBind const& home, CampaignHomeAnchor const& anchor,
                              float townYards);

// Is this a verdict a drive should answer by walking the member to an inn? The
// form the call site asks, so "does this home need moving" reads as a question
// rather than as a comparison against two enumerators - and so a fifth verdict
// added later cannot be silently left out of one of the two readings.
bool CampaignHomeNeedsRebinding(CampaignHome verdict);

// WHY THE FALL BASELINE GUARD DECLINED, AS A NUMBER THAT CAN BE COUNTED.
//
// #266 deployed the invariant that a character which is not falling is
// standing somewhere, and a character that is standing somewhere owes nothing
// for having got there, so every poll hands the core a baseline at that
// character's own feet. Phantom fall deaths continued anyway, and the reason
// turned out not to be the rule.
//
// FallBaselineStep declines in exactly one circumstance, `!mayInspect ||
// falling`, and its call site sits ABOVE the recovery's own stand-down and is
// not gated by the episode cooldown. The drive polls once a second, so if the
// guard ran, `m_lastFallZ` could not be more than one second of movement from
// the character's feet. The deaths measured on 2026-09-06 require it to have
// been 69 or more yards away: 0.018 * z_diff - 0.2426 reaches a full health
// bar at 69.03 yards, and two of those rows had a measured descent of exactly
// zero. Both cannot be true, so the guard was not called. It is correct and it
// is not running, and those two look identical in the data.
//
// SO THIS RECORDS WHICH INPUT DECLINED IT, and it is a mask rather than a
// first-match answer on purpose: "the flags say falling AND the flags say
// flying" is itself the diagnosis, and a rule that reported whichever the code
// tested first would hide exactly that. Every reason that is true is set.
//
// AND THE ABSENCE OF A READING IS NOT A READING. A mask of zero means the
// drive looked and nothing stood it down, which is a finding. Never having
// looked is a different finding and is carried as a NEGATIVE sentinel by the
// caller, the same way `recovery_rung` and `yards_fallen` already do. Folding
// the two together is how 223 kill-plane deaths went unexplained
// (2026_09_05_02_overseer_death_context.sql), so they are kept apart here too.
//
// WHAT THIS IS FOR, BEYOND THIS ONE BUG. The module stands down on movement
// FLAGS and the core charges fall damage on the absence of AURAS, and those
// are different questions. `Unit::IsFlying()` is
// `HasMovementFlag(MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY)`
// (Unit.h:1717); `Player::HandleFall` charges unless `HasHoverAura()`,
// `HasFeatherFallAura()` or `HasFlyAura()` (Player.cpp:14187-14189). A flag
// set without its aura is a state where the core will charge this character
// and this module has decided it has no opinion. Two core facts make that
// state easy to enter and hard to leave: `EffectMovementGenerator::Finalize`
// opens with `if (!unit->IsCreature()) return;`, so a player's
// MOVEMENTFLAG_FALLING is never cleared server-side and this roster sends no
// client packets to clear it; and `Player::TeleportTo` reduces the flags to
// MOVEMENTFLAG_MASK_HAS_PLAYER_STATUS_OPCODE (UnitDefines.h:423), which drops
// FALLING but KEEPS DISABLE_GRAVITY, CAN_FLY and HOVER, so a stand-down caused
// by one of those survives every death and every revival.
//
// This tells us which. It does not fix it, and deliberately changes no
// decision: every value here is written down and nothing reads it back.
enum FallGuardStandDown : uint16_t
{
    // The drive looked and nothing declined it, so the baseline was handed
    // over. Zero is a reading and not an absence.
    FALL_GUARD_RAN = 0,

    FALL_GUARD_DEAD        = 1u << 0,  // !alive
    FALL_GUARD_TELEPORTING = 1u << 1,  // Player::IsBeingTeleported
    FALL_GUARD_IN_FLIGHT   = 1u << 2,  // Player::IsInFlight, a taxi
    FALL_GUARD_FLYING      = 1u << 3,  // Unit::IsFlying, FLYING | DISABLE_GRAVITY
    FALL_GUARD_FALLING     = 1u << 4,  // Unit::IsFalling, FALLING | FALLING_FAR | a falling spline
    FALL_GUARD_IN_WATER    = 1u << 5,  // Unit::IsInWater
    FALL_GUARD_TRANSPORT   = 1u << 6,  // on a boat or a zeppelin
    FALL_GUARD_VEHICLE     = 1u << 7,  // in a vehicle

    // THE GUARD'S OWN TWO, ADDED WHEN IT STOPPED TRUSTING THE FLAGS (#291).
    //
    // Bits 0 to 7 keep the exact meanings they were deployed with, so rows
    // either side of that change can still be compared. What changed is which
    // of them DECIDE anything: after #291 the guard declines on bits 0, 1, 2,
    // 5, 6, 7 and on the two below. Bits 3 and 4, flying and falling, are
    // still recorded and no longer decline it, because they are the two the
    // measurement showed cannot be trusted. A reader wanting "did the guard
    // run" asks whether any bit OTHER than 3 and 4 is set.
    FALL_GUARD_NO_CHARGE   = 1u << 8,  // an aura the core itself checks: the fall is free
    FALL_GUARD_DESCENDING  = 1u << 9,  // measured to be losing height fast enough to be a fall
};

// EVERY REASON THAT IS TRUE, not the first one found. The argument order is
// TerrainRecoveryMayInspect's, unchanged, so the two cannot drift apart
// without a compiler noticing that one of them has the wrong arity.
uint16_t FallGuardStandDownMask(bool alive, bool teleporting, bool inFlight,
                                bool flying, bool falling, bool inWater,
                                bool onTransport, bool inVehicle);

// The same mask as something a person reading a log line can act on, lowest
// bit first and joined with '|', or "ran" when nothing declined it. The COLUMN
// keeps the number, because the question this exists to answer is "group the
// deaths by why the guard did not run and count them"; this is for the one
// line in the log beside it, so a reader does not need a lookup table at three
// in the morning.
std::string FallGuardStandDownNames(uint16_t mask);

// -------------------------------------------------------------------- mail --
//
// THE PARTS OF kind='mail' THAT NEED NO WORLD: reading the command text, the
// postage arithmetic the core does before it will take a letter, the
// cross-account delivery delay rule, and whether a refusal is worth trying
// again unchanged.
//
// WHY THESE ARE HERE AND NOT NEXT TO DoMail. The executor in mod_overseer.cpp
// drives WorldSession::HandleSendMail, HandleMailTakeItem, HandleMailTakeMoney,
// HandleMailReturnToSender and HandleMailDelete. Every one of them answers the
// CLIENT - SMSG_SEND_MAIL_RESULT on the session, a void return, or, for the
// structural refusals (no mailbox in reach, an empty recipient name), nothing
// at all. A bot has no client, so the module has to say BEFORE the call which
// wall a row is about to hit. Most of those tests are questions about the world
// (is there a mailbox in reach, is the item soulbound, does that mail id
// exist). The ones below are not: they are text and arithmetic, and a rule like
// "an item mailed to another account waits an hour and one mailed inside the
// account does not" is exactly the kind of rule nobody checks if checking it
// needs a running worldserver.
//
// THE GRAMMAR, one verb per row. `target_name` is the character acting;
// `target_arg` carries the RECIPIENT for `send`, the same way kind='give',
// kind='trade' and kind='share' name the other character, and is unused by the
// other four verbs.
//
//   send [item:<item_instance.guid>] [money:<copper>] subject:<text> [body:<text>]
//   take-item mail:<mail.id> item:<item_instance.guid>
//   take-money mail:<mail.id>
//   return mail:<mail.id>
//   delete mail:<mail.id>
//
// The `key:value` pairs come first, in any order, each at most once, and every
// value is a decimal id or a copper amount - there is no gold/silver notation
// because the Python side already speaks copper everywhere else.
//
// THE TEXT TAIL is what makes this grammar different from the auction one, and
// it is why the split is done here rather than with a regex on the Python side:
// a mail has two free-text fields and the row has one column. `subject:` opens
// the tail and runs to the first ` body:` after it, or to the end of the line;
// `body:` runs to the end of the line. Both are trimmed. A subject is required
// even for a letter that carries an item, because a subject is the only part a
// character sees in the mailbox list without opening anything, and a blank one
// is how mail becomes invisible clutter. There is deliberately no escape for a
// literal " body:" inside a subject: the first one splits, which is stated here
// and pinned by a test rather than left for somebody to discover.
//
// ONE ITEM PER LETTER. The packet carries up to MAX_MAIL_ITEMS (12) and the
// handler will take them all, but every other executor in this module moves ONE
// named item (kind='give', kind='sell', kind='bank') and the read-back is exact
// because of it. Postage is 30 copper per item either way
// (MailHandler.cpp:163), so twelve letters cost what one twelve-attachment
// letter costs, and twelve rows say twelve separate true things instead of one
// row that has to explain a partial failure.
//
// WHAT IS NOT HERE AND WHY. Reading the mailbox is not an executor: the `mail`
// and `mail_items` tables already hold every message, its sender, its money and
// its attachments, and reading them changes nothing in the world. Whatever
// drives the queue reads them and then queues a `take-item` by id. Marking a
// mail read is not an executor either - it moves one flag no other decision in
// this module reads. Creating a text item out of a letter (the "make a copy"
// button, HandleMailCreateTextItem) is not wanted by anything.

enum class MailVerb
{
    None,       // did not parse; `error` says why
    Send,
    TakeItem,
    TakeMoney,
    Return,
    Delete,
};

struct MailRequest
{
    MailVerb verb{MailVerb::None};
    uint32_t mailId{0};      // take-item, take-money, return, delete
    uint32_t itemGuid{0};    // send: the carried item to attach
                             // take-item: the attachment to take
    uint32_t money{0};       // send: copper to enclose, 0 for none
    bool hasItem{false};     // send: an item: key was given
    bool hasMoney{false};    // send: a money: key was given
    std::string subject;     // send: required, already trimmed
    std::string body;        // send: may be empty
    // Empty when it parsed; otherwise one of the MailRefusal literals below,
    // which have static storage so the executor can hand it on as `detail`
    // without copying.
    char const* error{""};
};

MailRequest ParseMailRequest(std::string const& command);

// The lengths the 3.3.5a client itself enforces in its own mail window before
// the packet is ever built: 64 characters of subject and 500 of body. The core
// enforces neither, and a longer one would be written to a `mail` row nothing
// could ever display. Named constants rather than literals in the parser
// because the test asserts on the boundary and a boundary in two places drifts.
constexpr std::size_t MAIL_SUBJECT_MAX = 64;
constexpr std::size_t MAIL_BODY_MAX = 500;

// WHAT A LETTER COSTS TO POST, as HandleSendMail works it out
// (MailHandler.cpp:163-175): 30 copper per attached item, 30 copper flat for a
// letter with none, plus whatever money is being enclosed. The handler then
// checks the sum for overflow (`reqmoney < money`, :168-172) and refuses
// silently, so the sum is computed here in 64 bits and the overflow is reported
// as a `false` the executor can name.
//
// It matters that this is the SUM and not the postage: a character with exactly
// the enclosed money and nothing over cannot post the letter, and "cannot
// afford the postage" is the only honest thing to say about that.
bool MailTotalCost(uint32_t money, bool hasItem, uint32_t& cost);

// WHEN A LETTER WAITS, which is the one piece of mail behaviour an operator has
// to plan around. HandleSendMail sets a delivery delay only when the letter
// CARRIES AN ITEM and the recipient is on a DIFFERENT ACCOUNT
// (MailHandler.cpp:350, :362); everything else - money, text, anything at all
// between two characters of one account - is delivered the instant it is sent.
// The delay itself is the realm's CONFIG_MAIL_DELIVERY_DELAY, an hour by
// default, and it is passed in rather than assumed so this file carries no copy
// of a number the realm owns.
//
// The family plays on named accounts, one per character, so mailing the greens
// to the tailor IS the cross-account case and the hour IS what happens. That is
// reported on the row (deliver_time, delivery_delay_seconds) rather than
// refused, because an hour late is what the game does and a refusal would be
// this module inventing a rule the world does not have. `take-item` on a letter
// that has not landed yet is what gets refused, by name, and retryably.
uint32_t MailDeliveryDelaySeconds(bool hasItem, bool sameAccount, uint32_t configuredDelay);

// THE REFUSAL LITERALS, in one place. Each is written into `detail` and into
// result.reason by DoMail, and MailRefusalRetryable below is keyed on them, so
// a literal that drifted between the two sides would silently turn a permanent
// refusal into one retried for ever. Keeping them here is what keeps the
// classification honest, and it is why the test exercises them by these names
// rather than by retyped strings.
namespace MailRefusal
{
// The grammar, decided without a world.
constexpr char const* Malformed         = "malformed mail command";
constexpr char const* NoSubject         = "mail has no subject";
constexpr char const* SubjectTooLong    = "subject longer than the client allows";
constexpr char const* BodyTooLong       = "body longer than the client allows";
constexpr char const* TextNotRenderable = "text carries a sequence the client cannot render";
constexpr char const* ZeroMoney         = "money is zero";
constexpr char const* MoneyTooHigh      = "money above the money cap";
constexpr char const* CodNotSupported   = "cash on delivery is not supported";

// The character doing the sending or the taking.
constexpr char const* NoSession         = "character has no session";
constexpr char const* NotInWorld        = "character is not in the world";
constexpr char const* Dead              = "character is dead";
constexpr char const* InFlight          = "character is on a flight path";
constexpr char const* Stunned           = "character is stunned";
constexpr char const* LoggingOut        = "character is logging out";
constexpr char const* InCombat          = "character is in combat";
constexpr char const* Trading           = "character is in a trade";
constexpr char const* BelowLevel        = "below the mail level requirement";
constexpr char const* NoMailbox         = "mailbox not in range";

// The other end of a `send`.
constexpr char const* NoRecipient       = "no recipient (put the receiving character in target_arg)";
constexpr char const* RecipientOffline  = "recipient is not online";
constexpr char const* RecipientIsSelf   = "recipient is the sender";
constexpr char const* RecipientFull     = "recipient mailbox is full";
constexpr char const* WrongTeam         = "recipient is on the other faction";

// The attachment.
constexpr char const* ItemNotCarried    = "item not carried";
constexpr char const* ItemNoTemplate    = "item has no template";
constexpr char const* ItemNotEmptyBag   = "item is a non-empty bag";
constexpr char const* ItemSoulbound     = "item is soulbound";
constexpr char const* ItemNotTradable   = "item cannot be mailed";
constexpr char const* ItemConjured      = "item is conjured or has a duration";
constexpr char const* ItemQuest         = "item is a quest item";
constexpr char const* ItemBeingLooted   = "item is being looted";
constexpr char const* ItemRefundable    = "item is still refundable";
constexpr char const* CannotAffordPost  = "cannot afford the postage";

// The mailbox side: a letter already received.
constexpr char const* MailNotFound      = "no mail with that id";
constexpr char const* MailNotDelivered  = "mail has not been delivered yet";
constexpr char const* MailDeleted       = "mail is already deleted";
constexpr char const* MailIsCod         = "mail is cash on delivery";
constexpr char const* MailFromSystem    = "mail was not sent by a character";
constexpr char const* MailAlreadyReturned = "mail was already returned";
// Deleting a letter DESTROYS what is on it: Player::_SaveMail issues
// CHAR_DEL_ITEM_INSTANCE for every attachment of a mail left in
// MAIL_STATE_DELETED. The client greys the button out for the same reason,
// and the core does not check, so these two are this module's own and the
// row that clears them is a take-item or a take-money, not a retry.
constexpr char const* MailStillHasItems = "mail still carries an attachment";
constexpr char const* MailStillHasMoney = "mail still carries money";
constexpr char const* AttachmentNotInMail = "mail does not carry that item";
constexpr char const* AttachmentMissing = "attachment missing from the mailbox";
constexpr char const* NoRoom            = "no room in the bags";
constexpr char const* NoMoneyInMail     = "mail carries no money";
constexpr char const* TooMuchGold       = "too much gold";

// After the handler ran and the world did not say what it should have.
constexpr char const* CoreRefused       = "the core refused the mail";
constexpr char const* NotReadBack       = "the mail did not read back";
}  // namespace MailRefusal

// IS THIS REFUSAL WORTH ASKING AGAIN WITHOUT CHANGING THE ROW. True for the
// walls that move on their own - the character is fighting, dead, stunned,
// mid-flight, mid-trade, broke, out of bag space, not yet standing at a
// mailbox, or waiting on a letter that has not landed. False for everything the
// same row will hit again for ever: a malformed command, a soulbound item, a
// mail id that is gone, a letter that is cash on delivery. Written into
// result.retryable so the sender can tell a "wait" from a "stop" without
// keeping its own copy of this module's strings; the give backoff
// (mod-overseer#169) is what happens when a sender cannot tell the two apart.
bool MailRefusalRetryable(std::string const& reason);

// ----------------------------------------------------------- hearth (#308) --
//
// WHAT A kind='hearth' ROW MAY SAY, AND WHY THE VERB IS THE ITEM.
//
// #286 gave this module the first half of the game's own cross-continent
// return: an innkeeper can now be asked to set a character's home, through the
// core's own binder handler, and the home is read back out of
// character_homebind rather than believed. This is the second half. A home is
// only worth having if something can go to it, and nothing here could.
//
// THE RETURN TRIP DID NOT EXIST. AGENTS.md records "sending a character home"
// as one of exactly two verbs seen returning `delivered` with a null result
// while changing nothing. That reading has been wrong about which verb it was
// for as long as it has been written down: upstream's `home` command is
// SetHomeAction, and SetHomeAction sets a home AT an innkeeper. It is the verb
// #286 replaced. It was never a way to travel to one. So this module has never
// had a "go home" of any kind, working or broken, and the count of things in it
// that can rejoin a family split across two continents has been zero.
//
// WHY NOT TeleportTo(m_homebind...). Because the module can already do that, in
// four places, and every one of them is a revival exit that had to argue for
// itself first. #286's migration named the rule this verb keeps: SetHomebind is
// public and would have worked, "and that is exactly the problem". The same
// sentence is true one step along. A teleport to the homebind asks nothing,
// costs nothing, cannot fail, and is an admin shortcut wearing the name of a
// game mechanic. AGENTS.md's standing instruction is to always fix the code and
// never reach for one. So this verb is the ITEM: the hearthstone in the
// character's own bags, its own spell, its own cast time, its own hour of
// cooldown and its own interrupts. A character in combat fails the way a player
// would, because it is the same code refusing.
//
// AND A BOT CAN ACTUALLY CAST ONE, WHICH WAS THE QUESTION THIS TURNED ON.
// Read at the pinned SHAs rather than assumed. WorldSession::HandleUseItemOpcode
// takes a raw WorldPacket, the same shape as the binder and areatrigger
// handlers this module already drives, and every client-facing thing it does
// funnels through WorldSession::SendPacket, which returns at `if (!m_Socket)`.
// A bot's session is constructed with a null socket, so those sends are no-ops
// and only the control flow matters. mod-playerbots already ships this exact
// call - UseItemAction::UseItem builds a CMSG_USE_ITEM by hand and hands it to
// bot->GetSession()->HandleUseItemOpcode - and it already has a hearthstone
// action on top of it. So the packet path is not novel; what is novel is
// judging it.
//
// THE ONE CLIENT-SHAPED PIECE, AND WHY IT IS ALREADY THERE. A cross-map
// teleport sets a far-teleport semaphore and then waits for the client to send
// MSG_MOVE_WORLDPORT_ACK. A bot has no client to send one. mod-playerbots fills
// that gap itself in PlayerbotAI::HandleTeleportAck, pumped every tick from
// PlayerbotHolder::UpdateSessions for any bot that reads IsBeingTeleported. So
// nothing has to be built here - but it is exactly why this executor refuses a
// character with no bot AI rather than casting anyway. Without one, a
// hearthstone across the ocean would leave the character wedged mid-teleport
// with nothing in the world to finish it, which is a worse outcome than any
// refusal.
//
// WHAT THAT COSTS, AND IT IS THE WHOLE DESIGN. A hearthstone is not instant.
// The cast time is the spell's, read off SpellInfo at runtime rather than
// written down here, and it is far longer than the poll that sent the row. So
// this executor cannot do what every other one in this module does, which is
// read the world back inside its own call and answer immediately. Pretending
// otherwise would produce exactly the status AGENTS.md warns about: a success
// reported before anything has happened, about a character that has not moved
// yet and may never. A hearth therefore parks in `verifying`, the same status a
// strategy command uses while its post-condition is read back, and the
// judgement below is what ends it.
//
// Column re-use, no new columns:
//   target_name  the character to send home
//   command      `use`, or empty, which means the same thing
//   target_arg   unused
//   detail       short refusal literal, or empty on success
//   result       JSON: outcome (arrived|stayed|elsewhere|refused|unreadable),
//                reason, retry (never|elsewhere|later - see
//                HearthRefusalRetry), character, item, spell, cast_ms,
//                window_ms, waited_ms, home, from and now each
//                {map, area, x, y, z} or null, request
//   status       'verifying' from the moment the cast starts, then 'applied'
//                when the character reads back at its home, 'unchanged' when it
//                never left, 'error' otherwise. NOT 'delivered', for the same
//                reason kind='bind' is not: a return trip that reports delivery
//                and moves nobody is the bug.
enum class HearthVerb
{
    None,  // not a hearth request; `error` says why
    Use,
};

struct HearthRequest
{
    HearthVerb verb{HearthVerb::None};
    std::string error;  // the refusal literal when verb is None, else empty
};

// ONE FORM, AND NO DESTINATION. Whitespace-tolerant, otherwise literal: the
// single lower-case word `use`, or an empty command meaning the same thing.
// There is deliberately no way to name a map, a coordinate or a town. The
// destination of a hearthstone is the home a previous kind='bind' wrote into
// character_homebind and nothing else, and a grammar that could ask for
// somewhere else would be a grammar for the teleport this verb exists in order
// not to be.
HearthRequest ParseHearthRequest(std::string const& command);

enum class HearthOutcome
{
    // The place the character started from, the home it was bound to, or where
    // it is now could not all be read. Or they could, and the first two are the
    // same place, so no reading of the third could tell an arrival from having
    // never moved. Says nothing about whether anything happened, and must never
    // be reported as any of the other three.
    Unreadable,
    // The character reads back at the home it was bound to. The cast finished
    // and the game moved it. The only outcome worth `applied`.
    Arrived,
    // The character is still standing where the cast began. THE FAILURE THIS
    // WHOLE EXECUTOR EXISTS TO MAKE VISIBLE: an interrupt, a refusal that
    // reported itself to a client that is not there, or a cast that never
    // started at all. From the queue those look identical, and not one of them
    // is `applied`.
    Stayed,
    // The character moved and is not at its home. A hearth went out and
    // something else decided where the character ended up: it died mid-cast and
    // released to a graveyard, or it was walking and kept walking. Kept apart
    // from Stayed because "it did not work" and "it went somewhere nobody asked
    // for" want different answers from the sender.
    Elsewhere,
};

// "arrived", "stayed", "elsewhere", "unreadable". Here rather than in the
// executor so the word a test pins is the word a row carries.
char const* HearthOutcomeWord(HearthOutcome outcome);

// A HEARTH THAT WOULD MOVE NOBODY CANNOT BE JUDGED, AND COSTS AN HOUR TO FIND
// THAT OUT. When the home is already where the character stands, `Arrived` and
// `Stayed` are the same reading and no post-condition can separate them. That
// alone would be reason to refuse. The cooldown is the other reason: a
// hearthstone that goes off spends an hour of the only cross-continent return
// this family has, and spending it to travel zero yards is the one use of it
// that can never be worth making. So the executor asks this BEFORE the packet
// and refuses, rather than casting and then reporting a verdict it already knew
// it could not reach.
bool HearthWouldMoveNobody(HomeBind const& standing, HomeBind const& home,
                           float arrivedYards);

// `from` is where the character stood when the cast began, `home` is that
// character's own bind read at the same moment, and `now` is where it is once
// the wait is over. Three readings, because "it is not at home" is only half an
// answer: whether it is still on the start line or somewhere else entirely is
// the half that says what went wrong. Distances are compared in three
// dimensions and only within one map, so a different map that is not the home
// map is `Elsewhere` however close the coordinates happen to land. That last
// clause is not pedantry: this family is split across exactly two continents
// which share a coordinate space, so a comparison that forgot the map would
// report the crossing as done while the character stood on the wrong side of
// the ocean.
//
// THE TWO TOLERANCES ARE DIFFERENT SIZES ON PURPOSE. `arrivedYards` is the
// wider one, and not because the spell is imprecise: Spell::EffectTeleportUnits
// puts the character on m_homebindX/Y/Z exactly. It is wide because nothing
// stands still afterwards. The judgement happens a margin after the cast ends,
// the character is a bot with a drive of its own, and a bot walks. Measuring
// arrival to the yard would report a character that landed at its inn and took
// four steps as having gone `elsewhere`. `movedYards` is the narrow one and
// answers a different question - did this character go anywhere at all - where
// a pace of drift is all that has to be absorbed.
HearthOutcome HearthReadBack(HomeBind const& from, HomeBind const& home,
                             HomeBind const& now, float arrivedYards,
                             float movedYards);

// HOW LONG TO WAIT BEFORE JUDGING, AND WHY IT IS NOT A CONSTANT.
//
// VERIFY_GRACE_MS is 6000, and a hearthstone's cast is longer than that, so
// reusing the strategy checks' window would judge every hearth this family ever
// casts as `Stayed` while the character was still standing there casting it.
// The window has to come from the spell.
//
// `castMs` is SpellInfo's own, read at runtime. `marginMs` is what the world
// needs after the cast ends: the teleport lands on a later tick than the one
// the cast completes on, a map change is not instant, and this module only
// looks every COMMAND_POLL_MS, so a window equal to the cast time would race
// the arrival it exists to see. `floorMs` answers a cast time that reads as
// zero, which is what a haste effect, a rank the DBC does not carry, and a core
// that resolved nothing all look like from here: a zero window judges instantly
// and therefore always answers `Stayed`.
//
// Saturating, not wrapping. A nonsense cast time out of the DBC must not become
// a short window by overflowing.
uint32_t HearthVerifyWindowMs(uint32_t castMs, uint32_t marginMs, uint32_t floorMs);

// Keyed on the `detail` literal the executor returns, grouped by what would
// have to change for the same row to succeed. Unknown is `Later`, the same call
// the bind, sell and repair tables make: a refusal this table has never heard
// of is more likely a new transient than a new permanent.
TownRetry HearthRefusalRetry(std::string const& detail);


// ------------------------------------------------------ A WAY ROUND (#316) --
//
// WHAT A GREEDY CONE CANNOT DO, AND WHY NO WIDER ONE WOULD HELP.
//
// GroundedStep picks a bearing out of a five-wide cone toward the aim and takes
// the best one it can prove ground under. It has no memory and no notion that a
// detour exists, so it cannot decide to walk AWAY from a destination in order
// to reach it - and walking away is exactly what going round a mountain is.
// Widening the cone does not add the missing idea; it only lets a character
// wander off its aim.
//
// Measured by replaying that same step chooser stride for stride over the
// SHIPPED heightmap grids: a character starting at Sishir Canyon and aimed at
// the Wailing Caverns door is refused every bearing after EIGHTY-FOUR YARDS,
// and one starting where the family actually stood is refused after 825. Three
// live runs ended the same way, at 2577, 2788 and 3022 yards out.
//
// AND THE CONTRAST THAT SAYS THE STEP CHOOSER ITSELF IS FINE. The same replay
// starting at Ratchet, 1465 yards of flat Barrens from the same aim, ARRIVES.
// Greedy is not broken. It is being asked a question a greedy search cannot
// answer.
//
// SO IT IS HANDED A NEARER QUESTION. A route is a sequence of points the greedy
// stepper can walk BETWEEN, each leg short enough and clear enough for it to
// succeed. Nothing about GroundedStep changes and nothing about the footing
// check changes: #312's refusals are reading real terrain and must keep doing
// it. The one thing that changes is that the cone is never again pointed at a
// destination on the far side of a mountain.
//
// WHERE THE POINTS COME FROM, AND WHY THIS MODULE DOES NOT INVENT THEM.
// mod-playerbots ships a surveyed travel node network as BASE DATA - 3781
// nodes, 15041 links and 1.4 million navmesh-walked waypoints, installed by its
// own SQL on any realm that runs it - and the waypoints inside a link sit about
// five yards apart. Somebody already walked these roads. These two decisions do
// the only two things reading that survey requires: choose a sequence of nodes,
// and choose which of the chosen legs' points to aim at this poll. Both are
// pure, so both are testable, and neither knows where the rows came from.
//
// WHY THE MODULE READS THE ROWS RATHER THAN ASKING THE OBJECT THAT HOLDS THEM.
// Upstream's own TravelNodeMap would answer this if it were populated, and at
// the pinned revision it is not: loadNodeStore() is called from exactly one
// non-debug place, TravelMgr::LoadQuestTravelTable, and that function has no
// callers anywhere in the tree. What startup actually runs is TravelMgr::Init,
// which builds the taxi graph and the destination cache and never touches the
// node map. Confirmed on the running realm, which logs "Playerbots Taxi graph
// and destination cache built." and has not one line mentioning travelNodes.
// The survey is real; the object that would serve it is empty.

// One node of the travel graph, as a caller read it out of wherever it keeps
// them. Plain floats and a plain id: these two files know nothing about the
// core's geometry classes and must not start now.
struct RouteNode
{
    std::uint32_t id{0};
    std::uint32_t mapId{0};
    float x{0.f};
    float y{0.f};
    float z{0.f};
};

// One ONE-WAY link between two nodes. The graph really is directed, so a caller
// with both directions must hand in both.
//
// `onFoot` IS THE WHOLE OF THE PARTY-TOGETHER RULE. Upstream types its links
// walk, portal, transport, flightPath and teleportSpell, and only a walk keeps
// five characters in one place. A flightPath carries one passenger and leaves
// the other four standing, which is exactly why the flight leg was declined and
// is still a good reason. A transport is a boat on a timetable. A portal changes
// maps, and nothing in this module can rejoin a party split across two of them
// (#241). The caller sets this flag and the search never second-guesses it: a
// link that is not walkable does not exist to it.
//
// Measured, so the cost of that rule is known rather than feared: on the shipped
// data 13885 of the 15041 links are walks, and all three routes replayed for
// this issue are walks end to end.
struct RouteLink
{
    std::uint32_t from{0};
    std::uint32_t to{0};
    // What walking it costs. Distance plus whatever the survey charges on top,
    // so a caller may price a link it would rather avoid without deleting it.
    float yards{0.f};
    bool onFoot{false};
    // WHETHER THIS LEG CROSSES GROUND THE OTHER SIDE GUARDS (#326). The caller
    // has measured it; the search only reads it, exactly as it does `onFoot`.
    //
    // "GUARDED" IS A FACTION WORD AND NOT A LEVEL ONE, and this module already
    // asks those as two separate questions. #300 asks whether the ground is
    // too high for the character, off spawn levels. This asks whether the
    // people standing on it attack the character for being the wrong faction:
    // the FactionStanceHostileTo test above, narrowed to a spawn whose own
    // faction group carries the OPPOSING player faction's bit, so a wild beast
    // hostile to everybody is not mistaken for a town that is hostile to one
    // side.
    //
    // Measured on the shipped world data for an Alliance party at the family's
    // own levels, 60 yard threat radius: of the 2478 walk legs on Kalimdor the
    // broad "anything lethal" reading marks 1592 and this faction reading marks
    // 647, and the leg the party actually died on twice is in both. The
    // narrower one is used because it is the question the deaths asked.
    bool guardedGround{false};
};

// One point along a leg. No id and no map: a route is walked on one map by
// construction, and these are only ever compared with a character's position.
struct RoutePoint
{
    float x{0.f};
    float y{0.f};
    float z{0.f};
};

// Why a route was not planned. Kept apart rather than collapsed into a bool,
// because they call for completely different things: NoGraph is a realm whose
// node tables were never populated and wants saying once for the whole process;
// NoNearerNode is about one aim and wants saying about that aim.
enum class RoutePlanVerdict : std::uint8_t
{
    // A node sequence was found. `nodes` holds it, entry node first.
    Planned,
    // No nodes on this map at all.
    NoGraph,
    // There are nodes, but none within `entryNodeYards` of where the character
    // stands. A character deep inside an instance, or out on a spit of coast
    // nobody surveyed, reads like this.
    NoEntryNode,
    // There is a way in, and nothing reachable ON FOOT from it is enough nearer
    // the aim to be worth walking to. Two different worlds arrive here and this
    // verdict alone does not separate them: a destination across an ocean, and
    // a character already standing at its errand.
    NoNearerNode,
    // The limits themselves were nonsense, so nothing was searched. Refused
    // rather than clamped, for the same reason TravelEndpointWithinTolerance
    // refuses a negative tolerance: a sign typo must not quietly become a rule
    // LOOSER than the one written.
    BadLimits,
};

char const* RoutePlanVerdictName(RoutePlanVerdict verdict);

struct RoutePlan
{
    RoutePlanVerdict verdict{RoutePlanVerdict::NoGraph};
    // Node ids, entry node first. Empty unless the verdict is Planned.
    std::vector<std::uint32_t> nodes;
    // What the planned legs cost, summed. A GRAPH COST AND NOT THE YARDS A
    // CHARACTER WILL WALK: the points inside a leg wander, and over the three
    // replayed routes the walk came out 8 to 21 per cent UNDER the summed link
    // distance, because the stepper cuts corners the survey did not.
    float yards{0.f};
    // How far the LAST node is from the aim, so a caller can say whether the
    // route finishes the journey or only most of it. Negative when nothing was
    // planned.
    //
    // IT IS ROUTINELY NOT ZERO, AND THAT IS THE HONEST ANSWER RATHER THAN A
    // SHORTFALL. The two nodes nearest the Wailing Caverns door are the
    // instance portal's own pair, and they sit in a ten-node island with no
    // walk link to the overland network at all. The nearest node a character
    // can actually WALK to is 343 yards out on open Barrens ground - which is
    // precisely the kind of last stretch the greedy stepper already crosses
    // unaided, and does in the Ratchet replay.
    float endsFromAimYards{-1.f};
    // HOW MANY LEGS OF THE PLAN STILL CROSS GUARDED GROUND (#326), and it is
    // not always zero. Going round is only taken when going round is not
    // farther, so a party whose only way to its errand runs through a guarded
    // place still gets that way, with this saying so, rather than getting
    // nothing. A caller that wants to refuse such an errand has the number to
    // refuse it on.
    std::uint32_t guardedLegs{0};
    // A way round was found AND taken. Not the negation of `guardedLegs`:
    // false also means "there was nothing to go round", which is the common
    // case and the one that must cost nothing.
    bool wentRound{false};
};

struct RoutePlanLimits
{
    // How far from the character a node may be and still be its way in. Wide
    // enough to find one from anywhere this family walks - the three measured
    // entries were 3, 107 and 129 yards out - and bounded so a character on the
    // wrong side of water does not adopt a node across it.
    float entryNodeYards{600.f};
    // THE MOST GROUND A WAY ROUND MAY HAND THE GREEDY STEPPER (#326), and the
    // one number in this rule, so it is measured rather than chosen.
    //
    // WHY IT HAS TO EXIST AT ALL. The rule above compares a way round with the
    // route it replaces on LEGS PLUS LEFTOVER, and that comparison treats a
    // yard of surveyed leg and a yard of greedy stepping as the same yard. They
    // are not: #316 exists because the greedy cone fails on terrain, which is
    // the whole reason legs are worth walking. Without a ceiling the comparison
    // will trade a route that ARRIVES for one that stops short of the aim, and
    // it does: replayed against the shipped survey, the Razorfen Kraul approach
    // reaches its door in 7415 yards through three guarded legs, and the
    // unbounded rule swaps it for 2095 yards of legs plus 4209 yards of greedy
    // walk, because 6304 is the smaller number. Less total ground is not a
    // shorter walk when the leftover is the part that does not work.
    //
    // AND WHY IT IS A THOUSAND. The measured population, all from #316's own
    // replays over the shipped heightmaps: the three routes it planned hand
    // over 343 yards and the stepper covers them; the Ratchet approach covers
    // 1465 yards of flat Barrens and arrives; the way round this issue needs is
    // 812 yards, and the last of those 812 holds nothing hostile at all.
    // Replayed across the five journeys this issue measured, every ceiling from
    // 1000 to 2000 gives the identical answer on all five, and every ceiling
    // under 812 gives up the fix. A thousand is the low end of the band that
    // works, so it leans toward keeping today's route - which is the direction
    // that cannot introduce a failure this module did not already have.
    //
    // Like RouteLimits::lethalRunYards, this is a first reading and the death
    // table is what says whether it was right.
    float roundHandoverYards{1000.f};
    // A route has to be worth walking. The last node must be at least this much
    // nearer the aim than the character already is, or there is nothing here it
    // could not do for itself, and aiming BACKWARDS is the greedy stepper's own
    // failure mode wearing a route's clothes.
    float minGainYards{200.f};
};

// THE NODE SEQUENCE, AND THE ONE CHOICE THAT MAKES IT WORK.
//
// Dijkstra over `onFoot` links only, from the node nearest the character to the
// node nearest the aim - except that "nearest the aim" is resolved AMONG THE
// NODES ACTUALLY REACHED, and that is not a detail. Measured: the nearest node
// to the Wailing Caverns door is 173 yards from it and is not reachable on foot
// from anywhere this family has ever stood, because it is an instance portal
// node in its own island. A planner that picks the goal by distance and then
// asks for a path answers "no route" for all three of the journeys measured
// here, and all three HAVE one. Picking the goal out of the reached set answers
// all three.
//
// ONE MAP. Nodes on another map are not read and links leaving it are not
// followed. A route that changes continents is a crossing, which this module
// does elsewhere and deliberately (#279).
//
// WHAT IT COSTS TO ASK. One search over one map's nodes - 616 of them on
// Kalimdor - run when an errand's aim changes and then kept for that errand,
// not run per poll. Two searches when, and only when, some leg is marked
// guarded; see the next paragraph for why the second one is skipped otherwise.
//
// ------------------------- AND IT MAY NOT WALK INTO THE OTHER SIDE (#326) --
//
// THE ROUTE WORKED AND IT KILLED THE PARTY, which is why this rule is about
// the route rather than about the walk. Watched live, an Alliance family of
// 26 to 32 converged on the Wailing Caverns door for the first time - 2236,
// 2071, 892, 749, 723, 683, 674 yards - and then walked back out to 2451 and
// into the next zone. The death table says why: `Barrens Guard` and `Horde
// Guard`, both level 40, both on the same two spots, twice each. The party
// revived at a graveyard behind the guards, walked the same route, and died
// in the same place. Zero runs in a night.
//
// THE SURVEY IS FACTION AGNOSTIC AND THAT IS NOT A BUG IN IT. It records where
// a character can physically walk, and its legs follow ROADS, because a road
// is where somebody walked. Roads are patrolled. Measured on the shipped data:
// the leg the family died on passes within 5 yards of a level 40 guard spawn,
// and the survey's own waypoints for it bend up to 274 yards off the straight
// line between its two nodes, so the guarded ground is in the MIDDLE of the
// leg and neither endpoint reads as guarded at any radius under 200 yards.
// The unit that can be marked is therefore the leg, which is why the flag is
// on RouteLink and not on RouteNode.
//
// WHAT WAS TRIED FIRST AND MEASURED WRONG, so nobody re-treads it:
//
//   * REFUSING EVERY GUARDED LEG. The only remaining way to that door is 24685
//     yards instead of 3594 - 58.8 minutes at 7 yards a second against a
//     staging window of twelve - and it goes through level 48 to 55 ground.
//     Avoiding a level 40 guard by walking past a level 55 elemental is not
//     avoiding anything.
//   * PRICING THE GUARDED LEGS INSTEAD. Flat: every surcharge from 100 to
//     10000 yards returns the identical route, because the alternative is not
//     a longer path to the same goal, it is a different continent's worth of
//     coastline. Pricing cannot move a search whose GOAL is fixed by distance.
//   * MARKING NODES RATHER THAN LEGS. Reproduces the leg reading for 579 of
//     647 legs but misses the one that mattered, and at the radius wide enough
//     to catch it (200 yards) it starts changing routes that already arrive.
//
// SO THE RULE IS ABOUT THE GOAL, AND IT HAS NO TUNING NUMBER IN IT.
//
//     A route may go round guarded ground only when going round is NOT
//     FARTHER, counting the legs it walks PLUS the stretch it leaves for the
//     greedy stepper.
//
// The second measure is the one that makes this work. The nearest node to that
// door is 343 yards out and can only be reached through the guards; a node 812
// yards out is reached with none, and the whole journey to it is 2993 yards
// against 3594. Legs plus leftover: 3805 against 3937. It is not a detour. It
// is shorter, and the last 812 yards hold nothing hostile at all.
//
// AND THE RULE CANNOT LENGTHEN A WALK, which is the whole of why it is safe to
// ship against routing that already works. `reach` is measured on the plan
// this module produces today, and a way round is adopted only when its own
// reach is no greater. A journey with no guarded leg anywhere skips the second
// search entirely and returns today's answer, unchanged and for the same cost.
// Replayed against the shipped survey and the dev realm's own world data: the
// Ratchet approach that #316 records as ARRIVING is unchanged, and so is the
// Blackfathom Deeps approach, which crosses no guarded ground at all.
RoutePlan PlanFootRoute(std::vector<RouteNode> const& nodes,
                        std::vector<RouteLink> const& links,
                        std::uint32_t mapId, float fromX, float fromY,
                        float toX, float toY, RoutePlanLimits const& limits);

// How far along its route a character has got. Held by the caller beside the
// route itself, and thrown away with it.
struct RouteCursor
{
    std::uint32_t at{0};
};

struct RouteAim
{
    // False means "this poll has no route point to offer", which the caller
    // must read as "aim at the errand itself" and never as an error.
    bool hasAim{false};
    // Within `arrivedYards` of the LAST point. The route is spent, and whatever
    // is left between here and the errand belongs to the greedy stepper, which
    // is the thing it is good at.
    bool arrived{false};
    std::uint32_t index{0};
    float x{0.f};
    float y{0.f};
    float z{0.f};
};

struct RouteLegLimits
{
    // How far ahead along the route to aim. THIS NUMBER HAS A MEASURED CEILING
    // AND A SECOND ONE READ OUT OF THE CORE, AND THEY AGREE.
    //
    // Replayed over the shipped heightmap grids, a lookahead of 120 and one of
    // 250 both arrive on all three routes; a lookahead of 400 is REFUSED after
    // 2812 yards on the longest of them, because a point that far ahead is once
    // again a straight line drawn across terrain and the cone is once again
    // pointing at a hill.
    //
    // Independently: PathGenerator cannot return a corridor longer than
    // MAX_POINT_PATH_LENGTH (74) points at SMOOTH_PATH_STEP_SIZE (4.0), which
    // is 296 yards (PathGenerator.h:32-42). Past that it sets PATHFIND_SHORT
    // and hands back a two-point shortcut (PathGenerator.cpp:685-690), which
    // NavmeshRoutes already refuses. So an aim beyond roughly 296 yards can
    // never come back as a route no matter how good the ground is. 250 sits
    // under both bounds.
    float lookaheadYards{250.f};
    // Near enough to the last point to call the route walked.
    float arrivedYards{12.f};
    // HOW MANY POINTS AHEAD THE AIM MAY BE, and zero means "as many as the
    // lookahead allows", which is what every caller had before this existed and
    // is still what a SURVEYED route wants (#344).
    //
    // THE TWO KINDS OF ROUTE WANT OPPOSITE THINGS HERE, which is why this is a
    // number and not a constant. A surveyed leg is a point list about five
    // yards apart - 162 of them for one 1029 yard leg - laid along a road that
    // is broadly straight, so aiming at the furthest point inside 250 yards is
    // aiming fifty points down a road and is exactly the corner-cutting that
    // makes the stepper efficient. A MEASURED corridor is the opposite: its
    // points stand a hundred and ninety yards apart and each one is written
    // down BECAUSE the straight line past it does not work. Aiming past one is
    // the mistake the corridor exists to prevent.
    //
    // AND ON ONE MEASURED WALK IT IS NOT A PREFERENCE BUT THE WHOLE DEFECT. The
    // descent into the ravine at the Wailing Caverns door is 465 yards of
    // walking that covers 172 yards of straight line: it leaves the terrace
    // EAST, loops south, and comes back WEST along the ravine floor. Every
    // polygon of it is within 220 yards of where it starts, so the entire
    // switchback fits inside one lookahead, and the existing rule aims at the
    // last point of it - straight across the ravine. That aim is 465 yards of
    // navmesh, which is over the 296 yards PathGenerator will smooth
    // (MAX_POINT_PATH_LENGTH 74 at SMOOTH_PATH_STEP_SIZE 4.0 under
    // MOD_PLAYERBOTS), so it comes back PATHFIND_SHORT as a two point shortcut,
    // NavmeshRoutes refuses it, and the party stands on the rim until the run
    // is closed. One point at a time, every leg is about 91 yards of navmesh
    // and every one of them answers.
    std::uint32_t maxPointsAhead{0};
};

// WHICH POINT TO AIM AT THIS POLL, AND THE TWO RULES THAT ARE THE WHOLE OF IT.
//
// THE CURSOR ONLY EVER MOVES FORWARD. A route walked backwards is not a route,
// and a character that drifts off the line - which it does, because the stepper
// is allowed to go round things - must not be sent back to the start by a point
// that happens to be near it.
//
// AND IT IS THE NEAREST POINT AHEAD, NOT THE LAST POINT ARRIVED AT. This is the
// rule that was got wrong first, and it failed loudly enough to be worth
// writing down: advancing the cursor only on "came within N yards of point i"
// STICKS, because a sixty-yard step steps clean over a point five yards wide
// and the cursor then never moves again. In the replay that version walked a
// hundred and eighty THOUSAND yards in circles and finished seventy yards from
// where it began. The cursor is a scan FORWARD from where it already is for the
// point nearest the character; the aim is then the furthest point still inside
// the lookahead.
//
// The forward scan gives up once points are running away by more than one
// lookahead, so a route that doubles back on itself does not cost a full pass
// every poll.
//
// NONSENSE LIMITS REFUSE rather than clamp, and a refusal here is `hasAim`
// false, which every caller already handles as "aim at the errand".
RouteAim RouteLegStep(RouteCursor& cursor, std::vector<RoutePoint> const& route,
                      float x, float y, RouteLegLimits const& limits);


// ------------------ a staging walk that is measured rather than routed (#342) --
//
// THE ROUTE PLANNER ABOVE IS RIGHT ABOUT ITS GRAPH AND WRONG ABOUT THE WORLD,
// and #326 says so in its own words: a survey leg follows a ROAD, because a
// road is where somebody walked, and roads are patrolled. When every way to a
// door runs down a patrolled road, PlanFootRoute reports the guarded legs and
// walks them, which is the only honest answer a search confined to those nodes
// can give. It is also, for one particular walk, fatal.
//
// WHAT IT COST. Six dungeon runs in a row ended `staging_failed` with the
// leader still more than a thousand yards from the door, and the death table
// says why: the leader was killed by level 40 guards of the other side, at
// level 30, on the road the route chose.
//
// AND THE DEATHS COMPOUND RATHER THAN COSTING ONE RUN EACH, which is the whole
// reason a staging walk is worth writing a corridor for. The comment on the
// revival-split rule above already says it in its own words: this zone is the
// other side's ground, so its graveyards stand beside the other side's guards,
// and for this family the loop closes on itself - die, revive at the
// graveyard, be killed by the guards standing at it. Nothing about being
// routed there in the first place is that rule's business. It is this one's.
//
// AND THE LEVEL GAP IS ALREADY IN THE MARK. `RouteLink::guardedGround` is set
// off a reading whose level floor is the character's own level plus
// CON_COLOR_UNKNOWN_LEVEL_DIFF - 1, narrowed to the opposing player faction. A
// marked leg therefore already means "the other side, and in the `??` band",
// not merely "hostile". There is no sharper question left to ask the graph:
// what is missing is a way that is not on it.
//
// ---------------------------------------------- SO THE CORRIDOR IS WRITTEN --
//
// A DOOR MAY CARRY A MEASURED WALK TO ITS APPROACH, AND THAT WALK IS WALKED
// RATHER THAN ROUTED. It is the same instrument #242 already uses one point of:
// where walkable ground is cannot be derived from any table in the world
// database, so the only honest way to have it is to measure the terrain, and a
// corridor that has NOT been measured must never be invented. This one was, and
// the measurement is quoted at the table itself so a reader can check it
// without believing this comment.
//
// WHY A BETTER AIM IS NOT ENOUGH, which is the part that decides the shape.
// Aimed by hand at a point in the middle of the safe ground, the travel layer
// still planned its way there through the guard post - "3 of its 4 legs cross
// ground the other side guards ... It walks it" - and turned a 408 yard hop
// into 2238 yards of surveyed leg by way of the road. The graph is not a
// suggestion the aim can steer; it is the whole route. So a corridor has to
// REPLACE the planned route, not feed it.
//
// WHAT THIS DELIBERATELY DOES NOT DO. It does not widen the footing check, it
// does not soften the faction reading, and it holds no opinion about any walk
// that has no corridor written for it: PlanFootRoute keeps every one of those,
// unchanged and for the same cost. Nor does it invent a general planner that
// leaves the survey graph, which would need a walkability oracle at runtime -
// exactly the thing #242 declined to invent, and for the same reason.
//
// ------------------------------- AND A CORRIDOR HAS TO REACH THE END (#344) --
//
// THE FIRST VERSION OF THIS RULE STOPPED AT THE TOP OF THE RAVINE, and the
// party stood there. It got the leader across the Barrens alive, which is what
// it was written for, and then two rules that are each right on their own threw
// away the part that gets DOWN:
//
//   * THE MINIMUM ROUTE LENGTH. RouteLeg drops a route once the character is
//     within TRAVEL_ROUTE_MIN_YARDS of its aim, because under that distance the
//     navmesh can answer and a survey adds nothing. The descent is 172 yards of
//     straight line. Every point of it was inside the circle, so the route was
//     dropped at the terrace and the staging point was handed over as a
//     bearing. A short leg is not a pointless leg when it is a descent, and the
//     minimum is now asked of the SURVEYED planner rather than of the layer.
//
//   * THE LOOKAHEAD. RouteLegStep aims at the furthest point inside 250 yards,
//     which is right for a surveyed leg whose points are five yards apart and
//     wrong for this one: the descent's whole switchback lies within 220 yards
//     of its own start, so the aim jumped straight across the ravine. See
//     RouteLegLimits::maxPointsAhead.
//
// WHAT THE GROUND ACTUALLY SAYS, because "the footing check refuses every
// bearing there" had to be answered rather than argued with. The way down is
// real and it is not a bearing: 465 yards of walking to cover 172 of straight
// line, leaving the terrace EAST, looping south, and returning WEST along the
// ravine floor. Judged against this module's own approach rule at every one of
// the 82 polygons of that walk, not one reads Overhead. The tightest stands 379
// yards along, 63 yards out and 17.8 up where the rule allows 21.0: 3.2 yards
// of margin, 15 per cent of the allowance. So the ramp passes, and it passes
// narrowly, and a leader who wanders off it is correctly refused.
//
// AND THE REASON A BEARING NEVER FOUND IT IS A BUDGET, not a gradient.
// PathGenerator smooths at most MAX_POINT_PATH_LENGTH (74 under MOD_PLAYERBOTS)
// points at SMOOTH_PATH_STEP_SIZE (4.0), which is 296 yards; past that it
// answers BuildShortcut() with PATHFIND_SHORT, a two point line through the
// wall, which NavmeshRoutes already refuses. The descent is 465 yards. Split
// into legs of about 91, every one of them answers. That budget is why a
// measured descent needs POINTS rather than one aim at the bottom of it.

// Why a corridor was not used. Kept apart rather than collapsed into a bool for
// the reason RoutePlanVerdict already is: "no corridor is written for this
// door" and "one is written and this character is nowhere near it" want
// completely different things said about them.
enum class StagingCorridorVerdict : std::uint8_t
{
    // The corridor is this walk's route. `route` holds it, join point first.
    Joined,
    // Nothing is written down for this walk. The common answer, and the one
    // that must cost nothing: three of the four doors in this module's own
    // portal table carry no corridor and need none.
    NoCorridor,
    // A corridor was offered and this walk is not going anywhere ON it, so it
    // is somebody else's corridor. Measured ground is only ever measured ground
    // for the walk it was measured FOR.
    NotThisAim,
    // THERE IS NO "THE AIM IS BEHIND THE JOIN" REFUSAL HERE ANY MORE, AND #356
    // IS WHY. This enum used to carry one, on the argument that a corridor is a
    // walk in one direction and handing its points back reversed would claim
    // something nobody measured. See PlanStagingCorridor below for why that
    // argument does not survive reading what the row actually measures, and for
    // the 72 minutes of deaths the refusal cost. A join past the aim is now the
    // same corridor walked the other way, and it is reported as `reversed`
    // rather than as a verdict.
    //
    // The corridor is real and this character is not near it. The join hop is
    // the one stretch of a corridor walk that is NOT measured ground, so it is
    // bounded rather than trusted; past the bound there is nothing honest to
    // say about walking it in one bearing.
    //
    // THIS IS NOT "THERE IS NO CORRIDOR FOR THIS WALK", AND READING IT THAT WAY
    // WAS THE OTHER HALF OF #356. The bound above is a true statement about one
    // hop and nothing more; the corridor is still the measured way to this door
    // and the character is still going there. So this verdict now reports
    // `joinIndex` and `joinYards` like a join that succeeded - which point is
    // nearest is a fact worth answering whether or not it is near enough - and
    // a caller that has a way of covering ground can use them to REACH the
    // corridor rather than to give it up. `route` stays empty, because there is
    // no walk to hand back until the character is on it.
    TooFarToJoin,
    // Two points of it stand further apart than one lookahead. That is a
    // deadlock and not a detour - see StagingCorridorLimits::maxLegYards - so
    // it is refused with a name rather than walked into.
    LegTooLong,
    // The limits themselves were nonsense, so nothing was judged. Refused
    // rather than clamped, the same way PlanFootRoute refuses BadLimits: a sign
    // typo must not quietly become a rule LOOSER than the one written.
    BadLimits,
};

char const* StagingCorridorVerdictName(StagingCorridorVerdict verdict);

struct StagingCorridorLimits
{
    // HOW NEAR A POINT OF THE CORRIDOR MUST BE TO THE AIM FOR THE CORRIDOR TO
    // BE ABOUT THIS WALK. This is what binds a written-down corridor to the
    // walk it was measured for, and it is tight on purpose: a staging aim is
    // formatted from floats that are either written in the same table row or
    // derived from two areatrigger rows, so it lands on its point exactly or
    // somebody has changed one of them without the other.
    //
    // ANY POINT AND NOT ONLY THE LAST, which is what lets one written corridor
    // serve the two legs of one approach (#344). A door with a measured descent
    // is walked at in two aims - the top of the descent first, the staging
    // point second - and they are the same walk with a stop in the middle. Two
    // corridors would be two chances to disagree about where that stop is; one
    // corridor with the stop ON it cannot disagree with itself. The route
    // handed back still ENDS at the aim, so nothing downstream can tell the
    // difference.
    float endsAtYards{5.f};
    // HOW FAR A CHARACTER MAY STAND FROM THE CORRIDOR AND STILL JOIN IT, and
    // this is the one number here that bounds unmeasured ground, so it is taken
    // from a measurement this module already made rather than chosen.
    //
    // It is the distance under which the travel layer already declines to plan
    // a route at all and walks straight at the aim (TRAVEL_ROUTE_MIN_YARDS).
    // The join hop is exactly that walk: unsurveyed ground crossed by bearing.
    // Bounding it by the module's own existing statement about how far a
    // bearing may be trusted means the two cannot drift apart, and it keeps the
    // hop shorter than the corridor's own narrowest clearance from a guard.
    //
    // Measured against the case this exists for: the leader's bind IS the
    // corridor's own first point, so the real join hop is nought yards.
    float joinYards{400.f};
    // NO TWO POINTS MAY STAND FURTHER APART THAN ONE LOOKAHEAD, and this is a
    // correctness bound rather than a preference.
    //
    // RouteLegStep aims at "the furthest point still inside the lookahead",
    // counted from the character. A character standing ON a route point whose
    // successor is beyond the lookahead is therefore handed its own feet, every
    // poll, forever: the cursor cannot advance because nothing ahead is near
    // enough to aim at, and nothing gets nearer because it is not being aimed
    // anywhere. That is the mirror image of the sticky cursor RouteLegStep's
    // own comment records walking a hundred and eighty thousand yards in
    // circles.
    //
    // A planned route never meets this, because the survey stores its legs as
    // point lists about five yards apart. A hand-written corridor easily could,
    // so it is asked rather than assumed, and the answer is a refusal with a
    // name on it rather than a run that stalls for twelve minutes and reports a
    // distance.
    //
    // The default is RouteLegLimits::lookaheadYards. A caller that walks its
    // routes with a different lookahead must pass its own; the corridor this
    // module ships stands 193 yards apart at its widest.
    float maxLegYards{250.f};
};

struct StagingCorridorPlan
{
    StagingCorridorVerdict verdict{StagingCorridorVerdict::NoCorridor};
    // Which point of the corridor the character joins at, or would join at if
    // it were near enough. Meaningful when the verdict is Joined and when it is
    // TooFarToJoin, and zero and meaningless for every other verdict, which is
    // the honest split rather than a tidy one: those two are the answers where
    // a nearest point was actually looked for, and the rest are refusals that
    // never got that far.
    std::size_t joinIndex{0};
    // How far the character stands from that point. Negative when no nearest
    // point was looked for at all - the same "no reading" convention
    // ApproachDistance already returns, and a value no caller can mistake for
    // having arrived. Under TooFarToJoin it is a real reading and it is over
    // `StagingCorridorLimits::joinYards`, which is what the verdict says.
    float joinYards{-1.f};
    // Which point of the corridor the aim itself stands on, and therefore the
    // last point of the route below. Zero and meaningless unless the verdict is
    // Joined.
    std::size_t endIndex{0};
    // WHICH WAY ALONG THE WRITTEN POINTS THIS ROUTE RUNS (#356). False is the
    // order the corridor is written in, which is the order it was measured
    // walking; true is the same points from a later index down to an earlier
    // one, which is what a walk BACK up the corridor is. `joinIndex` is greater
    // than `endIndex` exactly when this is true, so it carries no information a
    // caller could not derive - it is here because the line an operator reads
    // about a corridor walk should say which way the party is going without the
    // reader having to compare two indices.
    bool reversed{false};
    // The widest gap between two consecutive points of the whole corridor,
    // which is the number LegTooLong was decided on and is worth having in the
    // line an operator reads either way.
    float longestLegYards{0.f};
    // The corridor from the join point to the aim's point, join point first.
    // Empty unless the verdict is Joined, and handed straight to RouteLegStep,
    // which is the whole point: a corridor IS a route, so it is walked by the
    // thing that already walks routes - one point at a time, because
    // RouteLegLimits::maxPointsAhead says why.
    std::vector<RoutePoint> route;
};

// THE CORRIDOR FOR THIS WALK, IF THIS WALK HAS ONE.
//
// `corridor` is the written-down points in walking order. `aimX`/`aimY` is the
// place the errand is walking to, and the two are checked against each other
// rather than assumed to agree: the aim has to STAND ON the corridor, so a
// corridor can only ever be used for a walk it was measured for, and no caller
// has to carry a second name for it.
//
// THE ROUTE RUNS FROM THE JOIN TO THE AIM AND STOPS THERE, so one written
// corridor serves every aim along its own length. Points past the aim are not
// handed back: they are measured ground, but they are not this walk.
//
// AND IT RUNS IN WHICHEVER DIRECTION THE AIM LIES (#356). This used to refuse a
// join that stood past the aim, on the argument that "a corridor is a walk in
// one direction; the ground it crosses was measured being walked that way, and
// handing back its points reversed would be claiming a second thing that was
// never measured".
//
// THAT ARGUMENT DOES NOT SURVIVE READING WHAT THE ROW MEASURES. Every number
// written beside the corridor this module ships is a property of a line segment
// and not of a traveller: the length of a leg, the multiplier the navmesh
// charges over that leg's straight line, and the distance from that leg to the
// nearest spawn hostile to this family. A segment has two ends and no
// direction, so a leg that stands 225 yards clear of the other side stands 225
// yards clear walked either way. The one reading that sounds directional - that
// all 82 polygons of the descent are Closing against the approach rule at the
// staging point - is a statement about POSITION relative to that point, so the
// same polygons stood on in the other order read the same, and a character
// walking AWAY from the door is not being judged by that rule at all.
//
// WHAT THE REFUSAL COST, measured on the dev realm over 72 minutes after a
// deploy. The campaign's own inn IS the corridor's first point, so the walk
// that corrects a wrongly bound member (#349) has its aim at index 0, and every
// character not already standing on it read a join past the aim and was
// refused outright. Over one ten minute window the corridor was refused five
// times and used none. The surveyed road taken instead passes within 5 yards of
// a level 40 guard spawn where the corridor's tightest point keeps 225 yards
// from anything of the other side, and 8 of the 11 deaths in that window were
// faction guards. One member reached 188 yards from the innkeeper before dying
// and resurrected 1967 yards away; another reached 379 and did the same. The
// bind was never corrected, so the campaign held and no run could start.
//
// AND THE COORDINATOR ALREADY ASKS FOR THIS WALK IN SO MANY WORDS. Its note
// beside the per-run approach state says the next run "starts at the top of the
// corridor again, because the party comes back out of the door onto the ravine
// floor and has to be walked out and round". Out of the ravine and round to the
// top IS this corridor walked from a later point to an earlier one. The one
// walk the coordinator says it needs was the one walk this corridor would not
// serve.
//
// NOTHING IS INVENTED BY WALKING IT BACK. The route still begins on a written
// point, still ends on the point the aim stands on, and still contains no point
// that was not measured. `reversed` says which way it runs so the line an
// operator reads says it too.
//
// AND BEING TOO FAR OFF IT IS A DISTANCE AND NOT A DISMISSAL (#356 again). The
// direction half of that issue was fixed and the reach half was not, and the
// reach half is the one that was still killing characters. Measured on the dev
// realm: a member standing 1361 yards from the nearest point of the corridor
// was sent to the top of the descent, read `TooFarToJoin`, and was handed the
// surveyed route for the WHOLE journey - including the last stretch, which is
// the corridor's own ground and is the one place a surveyed route is known to
// run past a level 40 guard spawn at five yards. The bound did its job, which
// is to say that ONE hop of 1361 yards must not be crossed on a bearing, and
// then the caller drew a conclusion the bound does not support: that a corridor
// a character cannot step straight onto is a corridor it cannot use at all.
//
// So the nearest point is reported under that verdict too, and the caller is
// left to decide how to cover the gap. This function still refuses to CLAIM
// that gap - it hands back no route, because it has measured nothing between
// the character and the corridor - and that refusal is exactly what makes the
// reading useful: "here is where the measured ground begins, and I am not
// telling you how to reach it" is a different sentence from "there is nothing
// here for you", and only the first one is true.
//
// THE JOIN IS THE NEAREST POINT, AND THE ROUTE IS EVERYTHING FROM THERE ON.
// Nearest rather than first, because a character halfway along its corridor -
// one that set out, was interrupted, and set out again - must not be sent back
// to the mouth. Everything from there on rather than the nearest point alone,
// because the cursor in RouteLegStep only ever moves FORWARD, so a route handed
// to it has to begin where the walking begins.
//
// THE WHOLE CORRIDOR IS MEASURED FOR LegTooLong, NOT ONLY THE PART WALKED. A
// corridor is a written-down fact about the world; if any of it is malformed
// the table is wrong, and answering with the half that happens to be well
// formed would hide that until the day somebody joins at the other end.
//
// AN EMPTY CORRIDOR IS NoCorridor AND NOT AN ERROR. It is what three of the
// four rows in this module's portal table say, and it is the right answer for
// them.
StagingCorridorPlan PlanStagingCorridor(std::vector<RoutePoint> const& corridor,
                                        float aimX, float aimY,
                                        float fromX, float fromY,
                                        StagingCorridorLimits const& limits);


// ------------------------------- a far teleport still in flight (#310) --
//
// A CHARACTER IN THE MIDDLE OF A CROSS-MAP TELEPORT IS NOT A CHARACTER THAT
// LEFT. This rule exists because kind='hearth' got it wrong on the first real
// cross-continent hearth it ever ran. That hearth WORKED - the character
// arrived on map 1 - and the row said:
//
//   status: error
//   detail: left the world before the hearth could be read back
//   result: {"outcome":"unreadable", ... "now":null}
//
// The mechanism, read out of the core rather than guessed. Player::TeleportTo
// on a map change calls RemoveFromWorld and SetSemaphoreTeleportFar, so for the
// length of the crossing IsInWorld() is false while the character is still very
// much there. ObjectAccessor::FindPlayerByName takes `bool checkInWorld = true`
// and DROPS exactly that character, so the read-back's own lookup returned null
// and the verdict logic read null as gone.
//
// Leaving the world IS what a successful cross-map teleport looks like from the
// world thread. So a lookup that misses is not evidence of anything on its own,
// and the readings have to be separated:
//
//   `inNameMap`        the character is still in ObjectAccessor's name map,
//                      which survives a teleport and does not survive a logout.
//                      Ask with FindPlayerByName(name, false).
//   `inWorld`          Player::IsInWorld(), false for the whole crossing.
//   `stillTeleporting` Player::IsBeingTeleported(), near or far.
//   `waitedMs`         how long this read-back has been waiting, in total.
//   `ceilingMs`        how long it is willing to go on calling a crossing a
//                      crossing. Not a guess at how long a teleport takes; a
//                      backstop, so a teleport that never lands still ends as
//                      an answer rather than waiting for ever.
//
// Pure, and shared by every read-back in this module that can outlive a
// teleport, so the next verb does not have to rediscover it the same way.
enum class TeleportFlight
{
    // Nothing is in flight and the character can be read where it stands.
    Landed,
    // Still crossing, and inside the ceiling. THE MIDDLE OF A SUCCESS. Ask
    // again; do not read a position and do not write a verdict.
    InFlight,
    // Still crossing, and past the ceiling. Judged by where it ends up rather
    // than waited on for ever - but named apart from Landed so a row can say
    // that the reading it was judged on was taken during a teleport.
    Stranded,
    // Not in the name map, or in it and neither in the world nor teleporting.
    // A logout, and the only one of the four that means what the hearth's
    // "left the world before it could be read back" said.
    Gone,
};

// "landed", "in-flight", "stranded", "gone". Here rather than in the executor
// so the word a test pins is the word a row carries.
char const* TeleportFlightWord(TeleportFlight flight);

TeleportFlight ReadTeleportFlight(bool inNameMap, bool inWorld, bool stillTeleporting,
                                  uint32_t waitedMs, uint32_t ceilingMs);

// --------------------------------------------- the summoning stone (#313) --
//
// Summon one absent party member to a dungeon's summoning stone, across
// continents, through the game's own mechanic.
//
// WHY THIS VERB EXISTS. This family is split across two maps, and #241, #274,
// #279, #303, #304 and #305 are all the boat, none of which has landed. #308's
// hearthstone cannot help the two members that need it: both are bound on the
// continent they are already stranded on, so their hearthstone moves them
// within the wrong one. The summoning stone is the only thing in 3.3.5a that
// moves a character between continents on somebody else's initiative, and the
// core already implements every step of it.
//
// THE CHAIN, read out of the pinned core and the live world database rather
// than from memory. Every id is measured, and the file that records the
// measurements is data/sql/characters/base/2026_09_08_00_overseer_summon.sql.
//
//   1. The stone is a GAMEOBJECT_TYPE_MEETINGSTONE (type 23), NOT a
//      GAMEOBJECT_TYPE_SUMMONING_RITUAL. It carries minLevel, maxLevel and
//      areaID and no participant count at all - that field belongs to type 18,
//      and assuming otherwise would have built this verb against the wrong
//      object.
//   2. GameObject::Use's MEETINGSTONE case resolves who is being summoned from
//      the CLICKER'S OWN SELECTION, through ObjectAccessor::FindPlayer, which
//      is not map scoped. It refuses a target that is not in the same raid and
//      refuses either party below the stone's minLevel. Then it casts the
//      meeting stone summon.
//   3. That spell triggers the summoning stone effect, which is a TRANS_DOOR
//      creating the portal gameobject and is CHANNELED by the summoner for the
//      portal's lifetime. The spell effect seeds the summoner as the ritual's
//      first participant, so exactly ONE more party member has to click.
//   4. The portal is a type 18 ritual whose castersGrouped is set, so the
//      second clicker must be in the same raid and must not be the summoner.
//      When the count is reached the ritual settles and then casts the summon.
//   5. The summon is SPELL_EFFECT_SUMMON_PLAYER. Spell's implicit target
//      selection for that effect reads the caster's selection AGAIN, live, at
//      the moment the ritual completes - which is why this module re-asserts
//      the summoner's selection on every poll while the ritual is pending,
//      rather than setting it once and hoping.
//   6. The effect sets a summon point on the absent character and sends it a
//      request. IT DOES NOT MOVE ANYBODY. The character has to ACCEPT, and a
//      bot has no client to press the button, so nothing in this fleet has ever
//      answered a summon. The accept is driven the way the hearth drives its
//      item use: the core's own handler, handed a packet it wrote itself.
//   7. Accepting teleports across the map, which is a FAR teleport, which needs
//      MSG_MOVE_WORLDPORT_ACK answered. PlayerbotAI::HandleTeleportAck is the
//      only thing in this fleet that answers it, so a character with no bot AI
//      is a hard refusal rather than a hopeful attempt.
//
// WHAT THIS IS NOT. It is not a teleport. Every step above is a packet handed
// to a handler a client would have driven, and every gate the core keeps is
// still kept: the level floor, the party check, the interaction distance, the
// participant count, the accept. A GM summon would be one line and would be
// exactly the admin shortcut AGENTS.md exists to forbid.
//
// Column re-use, no new columns:
//   target_name  the SUMMONER: the character that stands at the stone and
//                clicks it. The actor, as in every other verb here.
//   command      `use <name>`, or bare `<name>`, naming the character to summon
//   target_arg   optional: the second clicker. Empty means "pick an eligible
//                party member already standing at the stone"
//   detail       short refusal literal, or empty on success
//   result       JSON: outcome (arrived|stayed|elsewhere|refused|unreadable|
//                walking|summoning), reason, retry (never|elsewhere|later - see
//                SummonRefusalRetry), summoner, summoned, helper, stone,
//                stone_entry, portal_entry, participants, required, flight (see
//                TeleportFlightWord), accepts, settle_ms, window_ms, waited_ms,
//                walked_summoner, walked_helper, approach_ms,
//                helper_stone_yards, at, from and now each {map, area, x, y, z}
//                or null, request
//   status       'verifying' from the moment the row starts walking its
//                clickers to the stone, and still 'verifying' through the
//                ritual; then 'applied' when the summoned character reads back
//                at the stone, 'unchanged' when it never moved, 'error'
//                otherwise. NOT 'delivered', for the same reason kind='hearth'
//                is not: a crossing that reports delivery and moves nobody is
//                the bug.

enum class SummonVerb
{
    None,  // not a summon request; `error` says why
    Use,
};

struct SummonRequest
{
    SummonVerb verb{SummonVerb::None};
    std::string who;    // the character to summon
    std::string error;  // the refusal literal when verb is None, else empty
};

// `use <name>`, or bare `<name>`.
//
// AN EMPTY COMMAND IS A REFUSAL HERE AND NOT A DEFAULT, which is the one place
// this parser deliberately differs from ParseHearthRequest. That verb has
// exactly one form and no arguments, so an empty column is unambiguous. This
// one has an argument, and a row that forgot it must not be answered by
// guessing which of four party members was meant.
SummonRequest ParseSummonRequest(std::string const& command);

enum class SummonOutcome
{
    // Where the character was, where the summon point is, or where it is now
    // could not all be read - or they could, and the first two are the same
    // place, so no reading of the third could tell an arrival from having never
    // moved. Says nothing about whether anything happened.
    Unreadable,
    // The summoned character reads back at the summon point. The only outcome
    // worth `applied`.
    Arrived,
    // The character is still standing exactly where it was when the portal was
    // clicked. THE FAILURE THIS EXECUTOR EXISTS TO MAKE VISIBLE, and there are
    // five ways to reach it that look identical from the queue: the second
    // clicker never channelled, the summoner's selection was lost before the
    // ritual settled, the request was never sent, the accept was refused
    // because the character was dead or in combat, or the far teleport was
    // never acknowledged. Not one of them is `applied`.
    Stayed,
    // The character moved and is not at the summon point. A summon went out and
    // something else decided where it ended up: it died and released, or its
    // own drive kept walking it. Kept apart from Stayed for the reason
    // HearthOutcome keeps them apart - "it did not work" and "it went somewhere
    // nobody asked for" want different answers from the sender.
    Elsewhere,
};

// "arrived", "stayed", "elsewhere", "unreadable".
char const* SummonOutcomeWord(SummonOutcome outcome);

// A SUMMON THAT WOULD MOVE NOBODY CANNOT BE JUDGED. When the character to
// summon is already standing at the summon point, `Arrived` and `Stayed` are
// the same reading and no post-condition can separate them. Refused before the
// packet for the reason HearthWouldMoveNobody is, minus the cooldown argument:
// a summoning stone has no cooldown, so the only cost of finding out the hard
// way is a wasted ritual and two characters' time.
bool SummonWouldMoveNobody(HomeBind const& summoned, HomeBind const& at,
                           float arrivedYards);

// `from` is where the summoned character stood when the portal was clicked,
// `at` is the summon point - which is the SUMMONER'S position and not the
// stone's, because the spell effect reads the caster's own coordinates - and
// `now` is where the character is once the wait is over.
//
// Three readings and the same two tolerances as HearthReadBack, for the same
// reasons: the wide one absorbs a bot that landed and took four steps, the
// narrow one answers whether the character went anywhere at all. Compared in
// three dimensions and only within one map, which is the whole point here - the
// two continents share a coordinate space, so a comparison that forgot the map
// would report a summon as done with the character still on the far side of the
// ocean.
SummonOutcome SummonReadBack(HomeBind const& from, HomeBind const& at, HomeBind const& now,
                             float arrivedYards, float movedYards);

// HOW LONG TO WAIT BEFORE JUDGING. `settleMs` is the ritual's own settle time,
// read off the core rather than invented, and `marginMs` is what the world
// needs after it: the summon request goes out on the tick the ritual completes,
// the accept is driven on the poll after that, and the teleport lands on a tick
// after THAT, while this module only looks every COMMAND_POLL_MS. `floorMs`
// answers a settle time that reads as zero, which would judge instantly and
// therefore always answer `Stayed`.
//
// Saturating, not wrapping, for the reason HearthVerifyWindowMs saturates.
uint32_t SummonVerifyWindowMs(uint32_t settleMs, uint32_t marginMs, uint32_t floorMs);

// Keyed on the `detail` literal DoSummon returns, grouped by what would have to
// change for the SAME row to succeed. Unknown is `Later`, the same call the
// bind, hearth, sell and repair tables make.
TownRetry SummonRefusalRetry(std::string const& detail);

// ------------------------------------ the last few yards to a stone (#355) --
//
// WHAT THIS DECIDES, AND THE MEASUREMENT THAT ASKED FOR IT. `kind='summon'` is
// this module's own stated answer for a member stranded on another continent -
// the home drive's refusal says so in as many words - and an operator could not
// land one in seven attempts over forty minutes on the dev realm on 2026-09-09.
// The reason is structural rather than luck: ONLY THE LEADER EVER REACHES THE
// STONE. A leader can be aimed, and on its aim it reached three yards. A
// FOLLOWER walks to the LEADER and stops at follow distance, which parked the
// two measured clickers 15 and 16 yards from the stone, and the gate the core
// keeps is GameObject::GetInteractionDistance - INTERACTION_DISTANCE for a
// meeting stone, about five yards. So a follower chosen as a clicker is never
// in reach however long anybody waits, and the two refusals the row produced
// said exactly that:
//
//     Bork 15y, Og 16y  ->  summoner is moving
//     Bork 55y          ->  no meeting stone within reach of the summoner
//
// THE HOLD WAS ALREADY THERE AND IT IS NOT THE MISSING HALF. #335 and #338
// built one hold for the three casting verbs and #350 put a dismount inside it,
// and the log shows all of it working. But a hold pins a character WHERE IT
// ALREADY STANDS, and where it stood was fifteen yards out. What was missing is
// the short walk that turns a hold at the wrong place into a hold at the stone.
//
// SO A ROW HAS THREE ANSWERS AND NOT TWO, and this is the whole of the rule.
// The executor supplies the core's own verdict on whether the stone can be
// clicked from where the character stands, the distance to the nearest stone it
// can see, how far this verb is willing to walk, and how long the walk has
// already had.
//
// THE GATE IS AN INPUT AND IS NOT RE-DERIVED HERE. The comparison the core's
// own handler makes is WorldObject::IsWithinDistInMap against the object's
// interaction distance: three dimensions, both bounding radii subtracted, and
// the map and the phase checked on the way past. A float comparison in this
// file could not answer that and must not pretend to, and a decision that
// disagreed with the handler by a hand's breadth would produce a row that walks
// to a stone, believes it has arrived, and has its click dropped in silence for
// ever. So the executor asks the core and passes the answer in, which is the
// same discipline every other decision in this file keeps about facts only the
// worldserver holds.
enum class SummonApproach
{
    // Nothing this verb can use. Either the sweep found no stone at all
    // (`nearestYards` negative, which is the executor's "never took a reading")
    // or it found one further off than this verb will walk anybody. Both mean
    // the same thing to a sender - stand somewhere else - which is why they are
    // one answer and not two.
    NoStone,
    // Inside the gate WorldSession::HandleGameObjectUseOpcode itself enforces,
    // so the ritual can be driven from where the character already is. This is
    // the answer every summon before #355 could ever get.
    Click,
    // Outside that gate and inside the walk. The clicker is walked the last few
    // yards and the row waits, which is the answer that did not exist.
    Walk,
    // The walk has had its time and the clicker is still not in reach. A real
    // answer rather than a row that walks for ever: something the executor
    // cannot see is holding the character up, and saying so with the distance
    // beside it is what the next attempt needs.
    OutOfTime,
};

// "no-stone", "click", "walk", "out-of-time". Here rather than in the executor
// so the word a test pins is the word a row carries.
char const* SummonApproachWord(SummonApproach approach);

// ARRIVAL IS ASKED BEFORE THE CLOCK, DELIBERATELY. A clicker that reaches the
// stone on the very poll its walk runs out has arrived, and answering
// `OutOfTime` about a character standing on the stone would throw away a summon
// that was about to work. The clock only decides between walking and giving up.
SummonApproach ReadSummonApproach(bool inReach, float nearestYards, float walkYards,
                                  uint32_t walkedMs, uint32_t ceilingMs);

// ----------------- the portal does not exist yet when the stone is clicked --
//
// THE THIRD WAIT, AND IT IS THE ONE THAT WAS MISSING ENTIRELY (#365). The verb
// clicked the meeting stone and looked for the ritual object in the next
// statement, which cannot work and never once did.
//
// WHAT THE CORE ACTUALLY DOES, at the pinned SHA. `GameObject::Use`'s
// MEETINGSTONE branch (GameObject.cpp:1902) casts spell 23598 on the clicker,
// and that spell is channelled - which the module's own evidence proves rather
// than assumes, because `GetCurrentSpell(CURRENT_CHANNELED_SPELL)` came back
// non-null and only `Spell::GetCurrentContainer` (Spell.cpp:8021) puts a spell
// in that container. `Spell::prepare` casts a spell immediately in exactly two
// places and a channelled one takes neither: Spell.cpp:3646 wants
// `!IsChanneled() || !GetMaxDuration()`, and Spell.cpp:3691 wants
// `GetCurrentContainer() == CURRENT_GENERIC_SPELL`. What prepare DOES do is
// `SetCurrentCastedSpell` (Spell.cpp:3665), which is the whole of why the
// channel reads as started. The cast itself is left to `Spell::update`
// (Spell.cpp:4430), and only inside that does `handle_immediate`
// (Spell.cpp:4036) reach `Spell::EffectTransmitted` (SpellEffects.cpp:5379),
// its `GAMEOBJECT_TYPE_SUMMONING_RITUAL` branch (SpellEffects.cpp:5466) and
// finally `AddToMap` (SpellEffects.cpp:5497).
//
// AND THIS MODULE'S POLL IS ON THE WRONG SIDE OF THAT. `World::Update` runs the
// maps at World.cpp:1242 and this module's hook at World.cpp:1342, so the spell
// is prepared after the map update that would have cast it. The portal first
// exists one world tick later, which is always after the poll that clicked the
// stone has finished refusing.
//
// SO THE ROW WAITS, exactly as it already waits for the walk and for the
// settle, and this is the reading that says how.
enum class SummonPortal
{
    // The portal is in the world and can be clicked. Asked first and before
    // anything else, because an object that exists is the answer whatever the
    // clock or the channel say about how it got there.
    Click,
    // No portal yet, and the summoner is still channelling the spell that makes
    // one. This is the ordinary answer on the poll after the click and the
    // reason this decision exists.
    Wait,
    // No portal, and no channel either. Waiting is waiting for nothing: the
    // ritual object is destroyed with the channel that owns it
    // (`m_caster->AddGameObject(pGameObj)`, SpellEffects.cpp:5470, removed at
    // spell cancel), so a summoner that has stopped channelling with no portal
    // in the world is a summon that is over.
    NoChannel,
    // Still channelling, still no portal, and the wait has had its time.
    // Something the executor cannot see is in the way, and saying so is what
    // the next attempt needs.
    OutOfTime,
};

// "click", "wait", "no-channel", "out-of-time". Here rather than in the
// executor so the word a test pins is the word a row carries.
char const* SummonPortalWord(SummonPortal portal);

// `channelling` is whether the summoner still holds the channelled spell, read
// off the core rather than inferred, and `waitedMs` is how long this row has
// been in this wait alone - not the walk's clock and not the ritual's.
SummonPortal ReadSummonPortal(bool portalSeen, bool channelling, uint32_t waitedMs,
                              uint32_t ceilingMs);
// ---------------------------------------------------- conjure (#147, #18) --
//
// WHAT A kind='conjure' ROW MAY SAY, AND WHY THIS VERB EXISTS AT ALL.
//
// THE MEASUREMENT. Read off the live realm on 2026-09-07, before any of this
// was written. Five characters, a hundred planned dungeon runs, zero of them
// run:
//
//   character  class    level  bag items  food  drink  money
//   ---------  -------  -----  ---------  ----  -----  ------
//   Bork       rogue      27      71        0     0    146 g
//   Grog       paladin    28      49        0     0    173 g
//   Grug       warrior    32      69        0     0    168 g
//   Og (lead)  mage       26      69        0     0    160 g
//   Ugga       priest     27      54        0     0    167 g
//
// Not one item of food or drink between them, and roughly 160 gold each. The
// zeroes are counted with the game's own classification and not with a list of
// item ids: item_template.spellcategory_1 is 11 for anything eaten and 59 for
// anything drunk, which is the same test upstream's own FindFoodVisitor makes
// (InventoryAction.cpp builds one on 11 and one on 59). Over the same evening
// the party died seven times in twenty one minutes, and two of the five are
// mana users who cannot restore mana between pulls with nothing to drink.
//
// THE OBVIOUS ANSWER IS THE WRONG ONE. They are not poor, so kind='buy' at a
// vendor looks like the fix. It is not the FIRST fix, because the party leader
// is a MAGE who has known how to conjure food since level 6 and water since
// level 4 and has never once done either. Conjured food and water are free,
// infinite, always level appropriate, and one mage supplies five characters.
// Every axis beats buying.
//
// WHY HE NEVER DOES IT, MEASURED IN UPSTREAM'S SOURCE RATHER THAN GUESSED.
// Both ends of the mechanism are already in mod-playerbots and neither is
// wired to the other:
//
//   * NoFoodTrigger and NoDrinkTrigger exist, are registered as "no food" and
//     "no drink", and their condition is written specifically for a conjurer:
//     "does this character carry any CONJURED food".
//   * CastConjureFoodAction and CastConjureWaterAction exist and are
//     registered as "conjure food" and "conjure water". There is even a branch
//     in CastSpellAction::Execute that exists only for those two names.
//   * No strategy in the whole tree ever pushes a TriggerNode naming any of
//     the four. The only NextAction("conjure ...") upstream carries is
//     "conjure mana gem".
//
// patches/mod-playerbots/0013-a-mage-never-conjures-food-or-water.patch is
// that missing wire, and it is the half of this fix that needs no row at all:
// with it a mage keeps ITSELF supplied, unprompted, for ever.
//
// SO WHY A VERB AS WELL. Because the trigger upstream wrote fires when the
// conjured stack is EMPTY, which keeps one character fed and cannot feed five.
// A party's worth of stock is a decision about a party: how many members, how
// many of them drink, how long the run is. That is not a bot's to take from
// inside its own appetite, and the numbers below are what taking it needs. A
// kind='conjure' row is how the side that knows those things asks, and the
// conjured items are BIND_NONE (measured: `bonding` is 0 on every Conjured *
// row in item_template), so kind='give' and kind='trade' can hand them out
// afterwards without anything new being built.
//
// WHAT ONE CAST PRODUCES IS NOT A NUMBER THIS FILE MAY WRITE DOWN, and the
// first version of this section proved it by writing one down and being wrong.
// It said "two items per three second cast", which is what the DBC's base
// points and die sides come to on paper. The running server reported TEN for
// the same spell and the same character through SpellEffectInfo::CalcValue,
// which is what the executor asks and what the effect actually creates.
//
// AND THEN IT REPORTED TWELVE, which is what settled it. Two live rows, same
// spell, same character, ten and then twelve. The mage was level 26 for the
// first and 27 for the second, and RealPointsPerLevel on every Conjure Food and
// Conjure Water rank is 2.00. So the count scales with the caster's level, by
// exactly that, and it changes under this module every time the mage levels.
// There is no number to write down here, only a call to make. (The paper
// arithmetic that gave two was wrong about a clamp; the running server is the
// authority and it always was.)
//
// The read-back counts what really landed in the bags, and a row that
// under-estimated ends as `short` and can simply be sent again. The cast time
// is three seconds and the items stack to twenty; those two are stable and are
// what the window and the target are built on.
//
// Column re-use, no new columns:
//   target_name  the character that will cast
//   command      `food` or `water`, optionally ` up_to:<units>`
//   target_arg   unused
//   detail       short refusal literal, or empty
//   result       JSON: outcome (casting|filled|short|nothing|refused|
//                unreadable), reason, retry (never|elsewhere|later - see
//                ConjureRefusalRetry), character, what, spell, item, per_cast,
//                wanted, carried_before, carried_after, casts_allowed,
//                casts_spent, window_ms, waited_ms, request
//   status       'verifying' from the moment the first cast goes out, then
//                'applied' when the bags read back at the target, 'unchanged'
//                when nothing was made, 'error' otherwise. NOT 'delivered':
//                a three second cast cannot be answered by the poll that
//                started it, which is the same reason kind='hearth' is not.

// THE GAME'S OWN TWO CATEGORIES FOR A CONSUMABLE THAT IS EATEN OR DRUNK.
// item_template.spellcategory_1 (ItemTemplate::Spells[0].SpellCategory), 11 for
// food and 59 for drink. Named here rather than written as literals at each use
// because they are the one pair of magic numbers this whole subject turns on,
// and because they are the reason no item id appears anywhere in this section:
// "what is food" is a question the world's own data answers, and a list of
// remembered ids would be a second answer that goes stale silently.
//
// This is not this module's invention. mod-playerbots asks exactly these two
// numbers of exactly this field in InventoryAction.cpp and again in
// PlayerbotFactory, and UseItemAction reads them to decide whether it is
// feasting or drinking.
constexpr uint32_t CONSUMABLE_CATEGORY_FOOD = 11;
constexpr uint32_t CONSUMABLE_CATEGORY_DRINK = 59;

// DOES THIS CLASS GET ANYTHING OUT OF A DRINK. A drink restores mana and
// nothing else, so it is worth exactly nothing to a class whose resource is
// rage or energy, and conjuring one for a warrior is a bag slot spent on a
// decoration.
//
// Taken from the class ids in the characters table (`characters`.`class`), the
// same numbers the class masks use. Mana: paladin (2), hunter (3), priest (5),
// shaman (7), mage (8), warlock (9), druid (11). Not mana: warrior (1), rogue
// (4), death knight (6). An unknown id answers false, because provisioning a
// class this module has never heard of is the case where doing nothing is
// right.
//
// THE HUNTER IS THE ONE WORTH SAYING OUT LOUD. In this expansion a hunter's
// shots cost mana rather than focus, so a hunter drinks. Carrying a later
// expansion's rule across is the obvious way to write this function wrong.
bool ClassRestoresManaByDrinking(uint32_t classId);

enum class ConjureWhat : uint8_t
{
    None,  // not a conjure request; `error` says why
    Food,
    Water,
};

// "food", "water", "none". Here rather than in the executor so the word a test
// pins is the word a row carries.
char const* ConjureWhatWord(ConjureWhat what);

struct ConjureRequest
{
    ConjureWhat what{ConjureWhat::None};
    bool capped{false};  // whether `up_to:` was given
    uint32_t upTo{0};    // units to END UP carrying; meaningful only when capped
    std::string error;   // the refusal literal when what is None, else empty
};

// ONE OF TWO WORDS, AND A TARGET THAT IS A TOTAL RATHER THAN A COUNT.
//
//     food [up_to:<units>]
//     water [up_to:<units>]
//
// `up_to` is what the character should END UP carrying, not how many casts to
// make and not how many to add. That is deliberate and it is the difference
// between a row that can be retried and one that cannot: a row saying "add 20"
// doubles the stock if it is sent twice, and this executor's whole design is
// that it may be interrupted part way and asked again. A row saying "have 20"
// is idempotent, and a repeat of it after a partial run finishes the job
// instead of overshooting it.
//
// Absent, the target is CONJURE_UNITS_DEFAULT.
//
// THERE IS NO WAY TO NAME A SPELL OR AN ITEM, for the same reason the hearth
// grammar has no way to name a destination. Which rank a character casts is
// decided by what it knows and what it may use, and a grammar that could ask
// for a particular one would be a grammar for casting a rank the character has
// outgrown or has not learned. The executor finds it by asking every spell the
// character knows what it CREATES and keeping the conjured consumables in the
// wanted category, which is also why a cooking recipe cannot be picked by
// mistake.
ConjureRequest ParseConjureRequest(std::string const& command);

// ONE STACK. Conjured food and water stack to twenty (measured: `stackable` is
// 20 on every Conjured * row in item_template), so twenty units is exactly one
// bag slot, and the bag slot is the binding constraint here rather than the
// gold: the roster's backpacks are full and the free space that exists is 8 to
// 13 slots in the equipped bags.
//
// TWENTY IS ALSO ABOUT THE RIGHT AMOUNT OF DRINKING. A conjured water at this
// family's level restores most of a level 26 caster's mana bar in one sitting,
// so twenty is roughly one drink every other pull across a dungeon run. It is
// not a precise number and does not pretend to be; it is one slot's worth,
// which is the smallest amount that is not obviously too little.
constexpr uint32_t CONJURE_UNITS_DEFAULT = 20;

// AND A CEILING ON WHAT A ROW MAY ASK FOR. Five stacks. Each unit costs half a
// three second cast, so a hundred units is two and a half minutes of a
// character standing still and casting, which is already longer than anything
// else this module asks of anybody. A row asking for more is refused as
// malformed rather than obeyed, because the likeliest reason for it is a typed
// extra zero and there is no way to interrupt a cast loop from the queue.
constexpr uint32_t CONJURE_UNITS_MAX = 100;

// WHAT ONE ROW IS ABOUT TO SPEND, worked out before the first cast goes out.
//
// `perCast` is what the spell's own effect creates, read off SpellInfo at
// runtime rather than written down here. `roomUnits` is how many more units the
// bags can actually take, which is not the same question as how many slots are
// free: an existing part-stack absorbs some for nothing.
struct ConjurePlan
{
    uint32_t wanted{0};       // units to end up with, after every clamp below
    uint32_t carried{0};      // units already in the bags
    uint32_t perCast{0};      // units one cast creates
    uint32_t casts{0};        // casts needed to get there; 0 means do nothing
    bool nothingToDo{false};  // already at or above the target
    bool roomLimited{false};  // the bags, not the request, set `wanted`
};

// Rounds UP: a target of 21 with two units a cast is eleven casts ending at 22,
// not ten casts ending at 20. Overshooting by less than one cast is free (the
// stack absorbs it) and undershooting means the row reports `short` for a
// remainder nobody could ever reach.
//
// A `perCast` of 0 is a spell whose effect this module could not read, and it
// produces a plan of zero casts rather than a division by zero or a loop that
// never ends. The executor names that case rather than casting into it.
ConjurePlan PlanConjure(uint32_t carried, uint32_t wanted, uint32_t perCast,
                        uint32_t roomUnits);

// WHAT TO DO ON THIS POLL, asked once per poll for as long as a row is parked.
//
// A conjure is not one cast. Ten of them, three seconds each, and the character
// can die, be pulled into a fight, be handed a travel errand or run out of mana
// at any point in the middle. So the resolver does not fire a fixed number of
// casts and hope: it reads the bags, asks this, and does exactly what it says.
// A WALKING CHARACTER CANNOT CAST THIS, AND THAT IS WHAT BROKE IT (#325).
//
// The first version of this loop shipped, ran on the realm, and made nothing.
// Six rows, honestly reported as `nothing`, no food. The reason is one this
// decision now has to know about, and it is not subtle once it is written down:
//
//   * Every Conjure Food and Conjure Water rank carries InterruptFlags 0x0F,
//     read out of Spell.dbc at the pinned build. Bit 0x01 is
//     SPELL_INTERRUPT_FLAG_MOVEMENT. Spell::prepare refuses a player with a
//     cast time who is moving (SPELL_FAILED_MOVING), and Spell::update cancels
//     a cast already preparing the moment the caster moves.
//   * mod-playerbots refuses it one layer earlier still: PlayerbotAI::CastSpell
//     opens with `if (bot->isMoving() && spell->GetCastTime())`, cancels the
//     spell, and returns false without ever calling prepare.
//   * These characters move nearly all the time. Measured off overseer_snapshot
//     on 2026-09-08: in one five second sample the party leader moved 2.8 yards
//     and another member moved 15.9. A three second cast has no chance.
//
// So the loop cannot simply try again and hope. Something has to make the
// character STAND STILL, and then the loop has to know the difference between
// "waiting for it to stop" and "casting and getting nothing", because those two
// want completely different answers and the first version reported both as the
// second.
enum class ConjureStep : uint8_t
{
    Cast,    // standing still, target not reached, budget left: cast again
    Wait,    // a cast is already in progress; do nothing this poll
    Settle,  // the character has been asked to stand and has not stopped yet
    Done,    // the bags hold the target
    GaveUp,  // see ConjureGiveUpReason for which of the three walls it hit
};

// "cast", "wait", "settle", "done", "gave up". Here rather than in the executor
// so the word a test pins is the word a row carries.
char const* ConjureStepWord(ConjureStep step);

struct ConjureProgress
{
    uint32_t carried{0};       // units in the bags right now
    uint32_t wanted{0};        // units the row asked to end up with
    uint32_t castsSpent{0};    // casts this row has already sent
    uint32_t castsAllowed{0};  // ConjurePlan::casts, plus whatever slack
    uint32_t idlePolls{0};     // consecutive polls that cast and gained nothing
    uint32_t idleLimit{0};     // how many of those before giving up
    uint32_t movingPolls{0};   // consecutive polls the character has not stopped
    uint32_t movingLimit{0};   // how many of those before giving up
    uint32_t castsRefused{0};  // casts the bot AI declined before preparing one
    bool castInFlight{false};  // the character is casting something right now
    bool moving{false};        // Unit::isMoving, which is what refuses the cast
    // THE WINDOW, ASKED HERE RATHER THAN AROUND THE OUTSIDE. The executor used
    // to hold this one itself and wrap every branch below in it, which meant
    // the window could end a row and have no say in what the row was allowed to
    // call it. A row that times out while it is still trying to stand still and
    // a row that times out having cast ten times are different sentences, and
    // only the side that knows both facts can pick between them.
    bool outOfTime{false};
};

// WHY A ROW STOPPED, when it stopped for a reason rather than by arriving.
// Four walls, and they are four different sentences to whoever sent the row:
// one is about the plan, one is about the spell, one is about the character's
// feet, and one is about a cast that was declined before it ever started. The
// first version of this verb had only "the casts produced nothing", and used it
// for all four - true every time, and the least useful true thing available.
//
// THE TWO THAT LOOK ALIKE AND ARE NOT. `CastRefused` is
// PlayerbotAI::CastSpell answering false, which it does without ever reaching
// Spell::prepare. `NothingAppeared` is a cast that really did start and really
// did finish and left the bags where they were. A sender can act on the first
// by changing what the character is doing; the second is about the spell.
enum class ConjureGaveUp : uint8_t
{
    NotGivenUp,       // the loop is still running or has finished properly
    NeverStoodStill,  // asked to stand, never did, so no cast was ever possible
    CastRefused,      // the bot AI declined every cast before one ever started
    NothingAppeared,  // casts went out, finished, and produced nothing
    BudgetSpent,      // every allowed cast was sent and the target is not met
};

// THE ORDER OF THESE TESTS IS THE DECISION.
//
// `Done` is asked FIRST, before `castInFlight`. A character that reached its
// target on the cast currently finishing is done, and asking about the cast
// first would park the row for one more poll to learn nothing.
//
// `Wait` is asked SECOND, before every give-up test, because a cast in flight
// is progress. Counting a three second cast against a budget measured in two
// second polls is how a working loop gets killed for being slow.
//
// THE IDLE TEST is the one that catches a spell that does not work. Out of
// mana, interrupted, bags filled by something else, a rank whose product this
// character may not use: from here all four look like a cast that goes out and
// produces nothing. AGENTS.md's standing rule is that `delivered` is not `done`
// and the world has to be read back; this is that rule as a loop.
//
// THE MOVING TEST is the one that catches a character that was never able to
// start. It is asked BEFORE `Settle` so that a character which has been asked
// to stand and has kept walking for the whole allowance ends the row rather
// than holding it open, and it is counted separately from the idle test so the
// two never get confused again. A `movingLimit` of 0 disables it, the same way
// `idleLimit` of 0 disables the other.
//
// AND `outOfTime` BEATS EVERYTHING INCLUDING A CAST IN FLIGHT. It is the
// backstop rather than the rule, and it has to be able to end a row whatever
// the character happens to be doing, because on the other side of this decision
// is a character being held still. A hold that outlives its window is this
// module stopping a character and forgetting it, which is a worse defect than
// the one that made this loop necessary.
ConjureStep ConjureNextStep(ConjureProgress const& progress);

// Which wall, for the same facts. Answers NotGivenUp whenever ConjureNextStep
// answers anything other than GaveUp, so the two can never disagree about
// whether a row ended.
ConjureGaveUp ConjureGiveUpReason(ConjureProgress const& progress);

// WHAT THE BOT AI IS ABOUT TO REFUSE ON, ASKED BEFORE IT IS ASKED (#329).
//
// PlayerbotAI::CastSpell answers a bare bool. It returns false when the caster
// is flying or on a taxi, when it is not standing (it stands the caster up and
// gives up that attempt), when it is moving and the spell has a cast time, and
// when Spell::prepare refuses - which for this spell is a cooldown or the
// global cooldown far more often than anything else. All four look identical
// from the other side of that bool, and #325 shipped a counter of them: a live
// row read `casts_refused: 4, casts_spent: 0` and the world held no record of
// which wall it was, four times over.
//
// So the executor reads these five facts off the character and asks this, and
// the row carries the answer. That is the same discipline kind='hearth' keeps -
// "every condition the handler would refuse on is named on this side first" -
// applied to a call whose refusal is a bool instead of a packet.
struct ConjureCastGate
{
    bool grounded{true};     // not flying and not on a taxi
    bool standing{true};     // Unit::IsStandState
    bool moving{false};      // Unit::isMoving, which refuses any timed cast
    bool spellReady{true};   // the spell's own cooldown has finished
    bool globalReady{true};  // ...and so has the global one
};

// "" when nothing here would refuse the cast, otherwise the literal for the
// FIRST thing that would, in the order the bot AI checks them. First and not
// all of them, because a row wants the wall it hit rather than a list.
char const* ConjureCastBlocker(ConjureCastGate const& gate);

// The refusal literal for each wall, so the word a row carries is decided here
// and not in the executor. NotGivenUp has no literal and answers "".
char const* ConjureGaveUpReasonWord(ConjureGaveUp reason);

enum class ConjureOutcome : uint8_t
{
    // The count in the bags could not be read at all, so no comparison means
    // anything. Says nothing about whether anything was made, and must never be
    // reported as any of the other three.
    Unreadable,
    // The bags hold what the row asked for. The only outcome worth `applied`.
    Filled,
    // More than before, less than asked. Something was made and something
    // stopped it: a fight, a corpse run, an empty mana bar, a full bag. Kept
    // apart from Nothing because "it half worked" and "the cast does nothing at
    // all" want different answers from the sender.
    Short,
    // No more than before. Every cast this row sent produced nothing, which is
    // the failure this whole executor exists to make visible.
    Nothing,
};

// "unreadable", "filled", "short", "nothing".
char const* ConjureOutcomeWord(ConjureOutcome outcome);

// `readable` is false when the item's template could not be read, which is the
// only way the count is genuinely unknown; an honest zero is a reading.
//
// A LOWER COUNT AFTER THAN BEFORE IS `Nothing`, NOT A NEGATIVE. The character
// is a bot with a strategy that eats, so it can consume the stack while the row
// is still running. Nothing was gained, which is what the word means.
ConjureOutcome ConjureReadBack(bool readable, uint32_t before, uint32_t after,
                               uint32_t wanted);

// HOW LONG TO LET A ROW RUN BEFORE JUDGING IT, and why it is not the hearth's
// window. A hearth is one cast; a conjure is `casts` of them, back to back,
// each landing on a later tick than the last, with this module's own poll
// interval between the end of one and the start of the next. So the window is
// per-cast and multiplied, not a constant.
//
// `marginMs` is what one cast needs beyond its own cast time: the tick the item
// lands on, and the poll that notices it. `floorMs` answers a cast time that
// reads as zero, which is what a haste effect and a DBC this core resolved
// nothing from both look like from here; a zero window judges instantly and
// therefore always answers `Nothing`.
//
// Saturating, not wrapping. A nonsense cast time or a nonsense cast count must
// not come out of here as a short window.
//
// `settleMs` was added by #325 and is the allowance for the character actually
// STOPPING. A conjure now asks the character to stand still first, and a bot
// under its own drive does not stop on the tick it is asked: the strategy has
// to take, the motion master has to run down, and this module only looks every
// COMMAND_POLL_MS. A window that did not carry that allowance would judge a row
// that spent its first ten seconds coming to a halt.
//
// `ceilingMs` was added by #329 and is the other half of the same thought. The
// window is now the maximum length of time a character is HELD STILL, and a
// hold is a cost the whole party pays: a follower stopped for a minute while
// its leader walks on has to catch up afterwards. The arithmetic above can
// reach a minute on a large target, so it is capped, and the cap wins over the
// floor when the two disagree because a ceiling that a floor can lift is not a
// ceiling. A ceiling of 0 means no cap.
uint32_t ConjureVerifyWindowMs(uint32_t castMs, uint32_t casts, uint32_t marginMs,
                               uint32_t settleMs, uint32_t floorMs, uint32_t ceilingMs);

// The refusal literals, all of them, in one place because they are what both
// sides of the queue read. None may carry a quote character: they go straight
// into the UPDATE.
namespace ConjureRefusal
{
// The row, or the character's spellbook.
constexpr char const* Malformed      = "malformed conjure command";
constexpr char const* CannotConjure  = "character knows no spell that conjures that";
constexpr char const* NoSpellInfo    = "the core does not know that spell";
constexpr char const* NoItemTemplate = "the conjured item has no template";
constexpr char const* CannotUseItem  = "character may not use what that spell conjures";
constexpr char const* MakesNothing   = "that spell creates no items";

// The character's own state, every one of which ends on its own.
constexpr char const* NoSession      = "character has no session";
constexpr char const* NotInWorld     = "character is not in the world";
constexpr char const* Dead           = "character is dead";
constexpr char const* InCombat       = "character is in combat";
constexpr char const* InFlight       = "character is on a flight path";
constexpr char const* Stunned        = "character is stunned";
constexpr char const* LoggingOut     = "character is logging out";
constexpr char const* Trading        = "character is in a trade";
constexpr char const* AlreadyCasting = "character is already casting";
constexpr char const* NoBotAI        = "character has no bot AI";
constexpr char const* NoRoom         = "no room in the bags";
constexpr char const* EnoughAlready  = "character already carries enough";

// After the casts went out and the bags did not move.
constexpr char const* NothingAppeared = "the casts produced nothing";
// ...and the three the first version of this verb was missing, every one of
// which it reported as the line above (#325).
constexpr char const* CastRefused     = "the cast did not start";
constexpr char const* NeverStoodStill = "character never stopped moving to cast";
constexpr char const* BudgetSpent     = "every allowed cast was sent";
// One row per character at a time, because two would both hold it still and
// the first to finish would let it walk away under the second.
constexpr char const* AlreadyRunning  = "a conjure is already running for this character";

// THE FOUR THE BOT AI REFUSES ON WITHOUT SAYING SO (#329).
// PlayerbotAI::CastSpell returns a bare bool. Every one of these makes it
// answer false, and #325 shipped a counter of those falses with no way to
// tell them apart: a live row read `casts_refused: 4, casts_spent: 0` and
// nothing in the world could say which wall it was. These are asked on this
// side, before the call, in the order the bot AI asks them itself.
constexpr char const* Moving          = "character is moving";
constexpr char const* NotStanding     = "character is not standing";
constexpr char const* SpellOnCooldown = "the spell is on cooldown";
constexpr char const* GlobalCooldown  = "the global cooldown has not finished";
// ...and the honest unknown, for the case where none of the four is true and
// the bot AI declines anyway. Naming it as an unknown is the whole point: a
// row that says this is a row that has ruled the other four out, which is a
// far better place to start than a bare count of nothing.
constexpr char const* CastDeclined    = "the bot ai declined the cast and named no reason";
}  // namespace ConjureRefusal

// WHETHER A REFUSAL IS WORTH ASKING AGAIN. Keyed on the `detail` literal, the
// same three classes the sell, repair, buy, bind and hearth tables use, and an
// unknown literal is `Later` for the same reason they give.
//
// NOTHING HERE IS EVER `Elsewhere`, and that is worth saying rather than
// leaving as an empty list somebody later reads as an oversight. Every other
// errand in this module's town trip has a place in it: a vendor, a repairer, a
// banker, an auctioneer, an innkeeper, a mailbox. A conjure has none. It is the
// one provisioning verb that works in the middle of a field, which is most of
// why it is worth having for a family that cannot reliably walk to a town.
TownRetry ConjureRefusalRetry(std::string const& detail);

// ------------------------------- holding a character still to cast (#335) --
//
// THREE VERBS NEED THE SAME THING AND ONLY ONE OF THEM EVER ASKED FOR IT.
// `conjure`, `hearth` and `summon` all cast something a movement interrupt
// cancels, and PlayerbotAI::CastSpell refuses a moving bot before the core is
// ever asked. #330 taught `conjure` to hold its caster still. `hearth` and
// `summon` were left observing `Unit::isMoving` and refusing, which for
// `summon` measured 29 refusals in three hours, every one `summoner is moving`.
//
// WHAT #330 STILL GOT WRONG, AND IT IS NOT THE PART THAT LOOKS WRONG. The hold
// removes `follow`, which is right - a follower is moved by its party rather
// than by its own drive. It is undone one poll later by this module's own
// roster sweep, which re-adds `follow` to any character that has a master and
// does not carry it, and grants `new rpg` to a leader that has lost it. That
// sweep exempts exactly one thing, the post-revival hold, because that was the
// only hold this module had when it was written. A cast hold was registered
// nowhere, so the sweep could not see one and handed the mover straight back.
// Two finished conjure rows say it in their own fields: `hold_took_stay` true
// and `hold_took_follow` false going in, `stay_at_end` false and
// `follow_at_end` true coming out, with nothing in the conjure executor having
// touched either.
//
// SO THE HOLD IS TWO THINGS AND THIS IS THE FIRST. What to change is a decision
// about three booleans and belongs here, where it can be pinned. Whether the
// sweep may hand a mover back is the other half, and it is ReadAimedMover's
// `heldToCast` below.

// What a hold found on a character's non-combat strategy list. All three are
// read from the live engine immediately before the hold acts, never remembered
// from an earlier poll: this strategy set comes and goes from writers that do
// not know a cast is in flight, which is the whole reason this decision exists.
struct CastHoldFacts
{
    bool hasStay{false};    // the strategy whose action stands a bot still
    bool hasFollow{false};  // moved by the party rather than by its own drive
    bool hasNewRpg{false};  // the self-drive: relevance 11, outranks everything
};

// What the hold changes, and therefore exactly what the release owes back. A
// hold that removes what it did not add, or restores what was never there, is a
// verb quietly rewriting a strategy set an operator was holding by hand - which
// is a thing that happened during #329's diagnosis and is why this is a record
// rather than a fixed list of calls.
struct CastHoldPlan
{
    bool addStay{false};
    bool dropFollow{false};
    bool dropNewRpg{false};
};

// The plan, given what is there.
//
// `flee` IS DELIBERATELY NOT IN THIS STRUCT AT ALL, rather than being a fourth
// false. A fleeing character moves, so `flee` fights any hold, and removing it
// would be a casting verb holding a character still while something kills it.
// #330 argued this for `conjure` and nothing about `hearth` or `summon` changes
// it. Leaving it out of the inputs is how that argument survives somebody
// reading only this struct.
CastHoldPlan PlanCastHold(CastHoldFacts const& facts);

// ------------- standing at the inn long enough to be bound there (#369) -----
//
// THE FIFTH REASON TO HOLD A CHARACTER STILL, AND THE SECOND THAT IS NOT A
// CAST. It exists because "an escorted character that has arrived holds there"
// was a sentence in a comment and not a thing the code did.
//
// WHAT THE SENTENCE CLAIMED. The travel drive's escort arrival branch
// deliberately does not release an errand on arrival, and says so: releasing
// would have an arrived place-aim call ChangeToIdle, and the next AI tick turn
// RPG_IDLE into a randomly chosen status, two of which pick a position out of
// the world and walk to it. Its answer was to fall through and let the walk be
// re-issued every poll, which it called holding.
//
// WHAT #346 ALREADY MEASURED ABOUT THAT ANSWER, at a different destination.
// Upstream ends the walk on arrival whether or not this module releases
// anything, so re-issuing is a race, and it is a race this module loses often
// enough to matter: four members at a dungeon door, sampled every fifty
// seconds, read 42/47/44/45 yards out, then 142/142/139/139, then
// 173/171/175/178, then 103/100/102/104, then 214/215/215/214. They never
// settle. The remedy was a hold, on the argument that taking the mover off is
// not a competition, and it was applied at exactly one of the module's arrival
// points.
//
// AND THE SAME SIGNATURE AT THE INN, MEASURED THE SAME WAY. A campaign will not
// open a run until every member is bound at the inn its dungeon is approached
// from, and two members spent twelve minutes each failing to be. Sampled from
// forced saves rather than from stale rows: one read 3 yards from the
// innkeeper, then 26, 265, 144, 160, 296, 783. The other read 261, 164, 146,
// 99, 406, 333, 959. One of them got to THREE YARDS and was not bound.
//
// WHY THAT IS ENOUGH TO FAIL EVERY TIME. The bind is attempted only on a poll
// on which the character is already inside the arrival radius, and that poll is
// the party poll, which is thirty seconds. Nothing holds the character inside a
// five yard circle for thirty seconds, so the two almost never coincide, and a
// walk that is re-issued cannot make them: an arrived character rolls a new
// status on the next AI tick, and a committed far move is not clobbered by the
// next re-issue while it still has ten yards to run. Twelve minutes of that is
// the backstop, and the backstop stands the character down for fifteen more,
// during which the campaign cannot start at all.
enum class InnHold : std::uint8_t
{
    // Nothing to do: it is not standing there and this module is not holding
    // it.
    Nothing,
    // Stand it still where it is, or re-asserted over a hold already there.
    Take,
    // Let go. Either it is not standing at the inn any more - dragged off by a
    // fight, which `stay` does not prevent because `stay` is a non-combat
    // strategy - or the bind has been refused here and standing still is no
    // longer useful.
    Release,
};

// THE REFUSAL IS AN INPUT, AND IT IS THE WHOLE SAFETY ARGUMENT FOR THIS.
//
// A hold is a promise that standing still is what gets this character bound. It
// is a true promise while the creature that binds is in reach and a false one
// the moment it is not: the arrival radius is measured against a point recorded
// in this module, and the bind is gated on the core's own interact check
// against the LIVE creature, which has feet. Where those two disagree - an
// innkeeper a few yards off its spawn row, or one of the other faction, which
// the core turns down however close a character stands - a hold would pin the
// character at the recorded point forever, and it would be this fix that
// stopped it ever getting lucky. So a refused bind lets go, and the drive that
// owns the walk is free to walk it again.
//
// AND `alreadyHeld` IS ASKED SO THAT LETTING GO IS SAID ONCE. Releasing a hold
// that is not there is a no-op with a log line attached, and a log line every
// poll about a character nothing is holding is how a real one gets buried.
InnHold InnHoldStep(bool atTheInn, bool bindRefusedHere, bool alreadyHeld);

// AND THE WAITING HALF IS NOT HERE, deliberately. A hold is not instant -
// StopMoving stops the spline and the flags Unit::isMoving reads clear on a
// later tick, and a live conjure row that started moving spent one settle poll
// before it could cast - but `conjure` already owns that decision in
// ConjureNextStep, and `hearth` and `summon` do not wait at all: they refuse
// with `later` and the sender re-asks with a fresh row, which is #230's rule
// about never recycling one in place. A second settle decision here would be a
// mechanism with no caller and a test pinning nothing.


// ------------- standing at a counter long enough to trade there (#378) ------
//
// THE SIXTH REASON TO HOLD A CHARACTER STILL, AND IT IS #369 AT THE OTHER END
// OF THE SAME TRIP. The inn hold above exists because "an escorted character
// that has arrived holds there" was a sentence in a comment rather than a thing
// the code did. This exists because "reached the vendor - errand done" is a
// sentence in a LOG LINE that is true for about as long as it takes to print.
//
// WHAT WAS MEASURED, off the module's own command table on the dev realm, all
// time. sell: 23025 error against 1608 delivered, 6.5%. repair: 52 against 15.
// buy: 20 against 6. bank: 66 against 0, never once. And one detail dominates
// every one of them - "vendor not in range" is 17200 of the 23025, "repairer
// not in range" 47 of 52, "banker not in range" 52 of 66. The remaining sell
// errors are about the seller rather than the counter: not online, item not
// carried, dead, in flight, count exceeds stack.
//
// WHY BEING IN RANGE IS THE THING THAT FAILS, IN TWO PARTS THAT COMPOUND.
//
//   1. NOTHING HOLDS THE CHARACTER AT THE COUNTER. The travel drive releases
//      the errand the poll it reads as arrived, and that release IS the signal
//      the pass outside the worldserver waits for before it writes any rows -
//      it clears `travel_npc`. So the transaction is asked for strictly after
//      the module has stopped holding on to the character, and by then upstream
//      has already ended the walk: an arrived aimed wander calls ChangeToIdle
//      and NewRpgStatusUpdateAction rolls RPG_IDLE into a randomly chosen
//      status on the next AI tick, two of which walk somewhere. This is the
//      identical mechanism #346 measured at a dungeon door and #369 measured at
//      an inn, and the identical remedy: a hold is not a race, and re-issuing a
//      walk is.
//   2. AND "ARRIVED" IS TWELVE YARDS, WHERE NO COUNTER WORKS. The arrival
//      radius for a creature errand is deliberately loose because it is
//      measured against a spawn point out of the creature table while the
//      creature patrols away from it, which is right for a flight master where
//      arriving IS the errand. A counter is not that. Every one of `sell`,
//      `repair` and `bank` calls the core's own GetNPCIfCanInteractWith before
//      it calls the core's handler, and that gate is INTERACTION_DISTANCE, five
//      yards. A character eleven yards from a vendor's spawn row has
//      "completed" its errand and cannot sell anything.
//
// SO ARRIVAL AT A COUNTER IS ASKED AS A DIFFERENT QUESTION, and it is asked of
// the LIVE CREATURE rather than of the recorded point. That is also why the
// answer has three values and not two: the case where the errand is not
// finished is not the same as the case where there is nothing there to finish
// it, and treating those alike is what would either strand a character in front
// of a corpse or send it away from a vendor standing eight yards off its spawn.
enum class CounterArrival : std::uint8_t
{
    // Release the errand, exactly as this branch always has. Either the aim is
    // not a counter at all, or it is and there is no creature of that role
    // anywhere near: the spawn is empty, dead or phased, and standing on it
    // achieves nothing that the pass which wrote the aim cannot do better by
    // writing another one.
    Done,
    // Do NOT release, and do not hold either. One of the role is nearby and
    // out of reach, which is the ordinary last few yards of the walk rather
    // than a fault - the same thing the doorway branch says about an arrival
    // radius wider than a trigger. Falling through re-issues the aim, and
    // upstream's own aimed wander closes the gap: it walks to `pos` until it is
    // inside INTERACTION_DISTANCE and then resolves the live creature within
    // INTERACTION_DISTANCE * 3.
    CloseTheGap,
    // Take the hold, then release the errand. The character is at the counter
    // on the core's own terms, so standing still is exactly what lets its rows
    // find it there.
    StandAndTrade,
};

// `inReach` IS THE CORE'S OWN ANSWER AND NOT A DISTANCE, and that is the whole
// safety argument, unchanged from #370's. A hold is a promise that standing
// still is what completes this errand. It is a true promise only while a
// creature that can serve the errand passes the same gate the executor will
// put it through, and false the moment it does not - an unfriendly vendor is
// turned down however close a character stands, and no amount of holding will
// change that. So the adapter asks GetNPCIfCanInteractWith with the role's own
// npcflag, which is the same call DoSell, DoRepair and DoBank each make, and
// passes the answer here rather than a number this could be tempted to compare.
//
// `oneIsNearby` EXISTS TO TELL "NOT YET" FROM "NEVER", and the adapter measures
// it at the radius upstream's own arrival will search rather than at whatever a
// sweep happens to see. Once an aimed wander is within INTERACTION_DISTANCE of
// the recorded point it looks for the creature with
// FindNearestCreature(npcEntry, INTERACTION_DISTANCE * 3) and idles if it finds
// nothing, so a counter further off its spawn row than that is a gap no loop is
// closing. Answering CloseTheGap for one would have the errand sit out its own
// twenty minute backstop waiting for a walk nobody is making, which is worse
// than releasing rather than better.
CounterArrival CounterArrivalStep(CounterRole role, bool inReach, bool oneIsNearby);


// ------------------- and a hold that is taken once is not a hold (#358) --
//
// THE HOLD ABOVE STOPS A CHARACTER BEING SENT SOMEWHERE AND DOES NOT STOP ONE
// ALREADY WALKING, AND FOUR VERBS SPENT THEIR WHOLE LIVES ASSUMING OTHERWISE.
// `conjure`, `hearth`, `summon` and `stage` all take that hold, and all four
// are written as though nothing were in flight when they do. Read off the
// pinned core rather than inferred: Unit::StopMoving (Unit.cpp:13045) clears
// UNIT_STATE_MOVING and stops the current spline and never touches the
// MotionMaster, so a FollowMovementGenerator that MoveFollow left in
// MOTION_SLOT_ACTIVE is still sitting there and re-splines on the next tick
// that finds its target out of position (TargetedMovementGenerator.cpp:580).
// Measured on the dev realm on 2026-09-09, a character whose log line reported
// every half of the hold going on - stay added, follow removed, already
// standing, taken off its mount - read fifteen yards from a meeting stone and
// fifty-five yards from it nine seconds later.
//
// WHAT THE ADAPTER DOES ABOUT IT IS TAKE THE ACTIVE MOTION SLOT, which is a
// call rather than a decision and so is not here. WHAT IS HERE IS THE ONE
// QUESTION THE SWEEP THAT KEEPS THE HOLD HAS TO ANSWER ON EVERY TICK - and
// every way of answering it wrongly is a separate defect, which is why it is a
// function with a test rather than four conditions in a loop.

// What is true about a held character at the moment the sweep looks.
struct HeldStillFacts
{
    // In the world and not mid-teleport. A name that resolves to nothing is a
    // character that logged out or is crossing a map; the hold's own release
    // and the ceiling sweep are what end that, not this.
    bool present{false};
    // The ceiling is up and the release is already owed. Re-taking a hold past
    // its deadline would be this module standing a character still for longer
    // than it ever said it would, which is the one thing the ceiling exists to
    // make impossible.
    bool pastDeadline{false};
    // A VERB IS WALKING THIS CHARACTER UNDER ITS OWN HOLD, which is #357 and is
    // the only input here that is not a fact about the world. The summon's
    // approach closes the last few yards onto a meeting stone with a MovePoint
    // issued deliberately, under the hold; a sweep that re-took the slot every
    // time a held character was somewhere other than where it started would
    // cancel that walk on the tick after it began, turning the fix for one verb
    // into a defect in the same verb.
    bool walkingOnPurpose{false};
    // FIGHTING. `stay` and `follow` are non-combat strategies, so a held
    // character still fights and still flees, and `flee` is left on the list
    // deliberately for exactly that reason: a hold that pinned a character
    // through a fight would be this module holding one still while something
    // kills it. That argument is older than this decision and applies with more
    // force to a pin that takes the slot the combat engine is steering with.
    bool inCombat{false};
    // HOW FAR IT IS FROM WHERE THE HOLD LAST MEANT, and this is deliberately
    // not "is it moving". Unit::isMoving reads a movement flag that the core
    // writes and clears inside the very calls a hold makes, so a sweep keyed on
    // it would be reading the hold's own working rather than the world's; and
    // it would answer nothing at all about a character that has been dragged
    // thirty yards and has just that moment stopped. A distance from a place
    // this module chose is a fact about the hold rather than about a flag.
    float driftYards{0.f};
};

// Does the hold have to be re-taken on this tick?
//
// THE SLACK IS A PARAMETER BECAUSE IT IS A DISTANCE THE ADAPTER MEASURES AND
// NOT A NUMBER THIS HEADER KNOWS. What this header can say about it is that it
// must be above zero: the call that takes a hold ends by stopping a spline, and
// stopping one recomputes the unit's position from it, so a character that has
// just been pinned can read a fraction of a yard from where its anchor was
// written. A slack of zero would have the sweep answering "it has drifted"
// about its own pin, every tick, for as long as the hold stood.
bool RetakeTheHold(HeldStillFacts const& facts, float slackYards);


// ------------------ what stopped a cast this module drove at the core (#337) --
//
// THE HOLD WORKS AND THE CAST STILL DOES NOT GO OUT, WHICH IS A DIFFERENT
// DEFECT WEARING THE SAME CLOTHES. Measured on the dev realm 2026-09-08, eight
// consecutive hearths on the party leader alternated: one row refused
// `character is moving` and placed a hold, the next found the character
// standing, drove the cast, and ended `stayed`. The log has the hold going on
// at 17:08:51 and coming off at 17:09:31, and the cast being driven at 17:09:15
// squarely between them. So the character was held, and the cast did not start:
// `casting_after_call` false, the hearthstone cooldown still clear at the
// verdict, and the character 1,596 yards from a home it never reached.
//
// AND THE ROW COULD NOT SAY WHY, WHICH IS #329 ALL OVER AGAIN. `conjure` drives
// its cast through PlayerbotAI::CastSpell and #330 taught it to name the wall
// it hit. `hearth` and `summon` drive theirs by handing a packet to the core's
// own handler, which answers a client that a bot does not have, and they record
// one bit: whether a cast was running immediately afterwards. When that bit is
// false the row has nothing at all to say.
//
// SO THESE ARE THE CORE'S WALLS RATHER THAN THE BOT AI'S, and they are asked in
// the order Spell::CheckCast reaches them. Same idea as ConjureCastGate, a
// different list, because a different thing is refusing.
//
// SITTING IS ON THE LIST AND IT IS THE ONE WORTH EXPLAINING. A real player's
// client stands them up before it sends CMSG_USE_ITEM; the packet this module
// hands the handler comes from no client at all, so nothing stands anybody up.
// That is the same shape as the areatrigger this module already had to send by
// hand: walking in like a player depends on a part of the player a bot does not
// have. And the hold makes it MORE likely rather than less, because a character
// that has stopped moving and is out of combat is a character its own `food`
// strategy will sit down and eat.
// AND FIVE WALLS WERE NOT ENOUGH, WHICH IS WHAT THE NEXT NIGHT MEASURED. Six
// more hearths on the same party leader, on the build carrying this gate, and
// four of them came back `the cast did not start and nothing in front of it was
// true`. The hold reported the character already standing, the hearthstone
// cooldown read clear at the verdict, and every one of the five below was clear.
// So the wall was real, it was outside the list, and the row had been given a
// better sentence for the same dead end.
//
// TWO MORE WALLS WERE MISSING AND BOTH ARE IN Spell::CheckCast AT THE PINNED
// SHA, read there rather than remembered:
//
//   * THE MOUNT (Spell.cpp:6018). The core refuses any non-passive spell cast
//     by a MOUNTED player that does not carry SPELL_ATTR0_ALLOW_WHILE_MOUNTED,
//     and it answers SPELL_FAILED_NOT_ON_TAXI when the caster is in flight and
//     SPELL_FAILED_NOT_MOUNTED when it is on a ground mount. `grounded` below
//     was therefore reading ONE BRANCH of a check whose other branch nothing
//     here had ever asked, and a ground mount is by far the commoner of the
//     two. A real player never reaches this line, because the client sends
//     CMSG_CANCEL_MOUNT_AURA before it sends the use: the same shape as the
//     areatrigger and the stand state, a part of the player a bot does not
//     have.
//
//     AND IT IS THE ONE THING THAT SORTS A LEADER FROM A FOLLOWER, which is the
//     discriminator those six rows carry and the reason this wall is named
//     first among the new ones. The same verb succeeded first try on a follower
//     the same evening. mod-playerbots' CheckMountStateAction mounts a bot
//     whose travel target is further away than its mount distance, and makes a
//     bot with a master mirror that master's mount state, so the character the
//     travel drive is walking across a continent is the mounted one and the
//     character standing beside an inn is not.
//
//   * THE GLOBAL COOLDOWN (Spell.cpp:5711). It returns SPELL_FAILED_NOT_READY,
//     Player::HasSpellCooldown cannot see it, and it is transient, which is
//     exactly the shape of a wall that hits four rows out of six and never the
//     same way twice. ConjureCastGate has asked it since #330 under the name
//     `globalReady`; `hearth` and `summon` never did, and the two gates being
//     different lists was itself the bug.
struct CastWallGate
{
    bool grounded{true};    // not flying and not on a taxi
    bool unmounted{true};   // ...and not on a ground mount either, which is the
                            // same core check answering its other branch
    bool standing{true};    // Unit::IsStandState - a bot has no client to stand it
    bool still{true};       // not moving, which cancels any cast with the flag
    bool ready{true};       // the spell's own cooldown has finished
    bool globalReady{true};  // ...and so has the global one, which the line above cannot see
    bool free{true};        // not already casting something else
};

// Which wall, or "" when the gate is clear. A cast refused with none of them
// true gets NoneNamed rather than a shrug: an honest unknown that has ruled the
// other walls out is a far better place to start than a bare false.
char const* CastWallBlocker(CastWallGate const& gate);

// THE LITERALS GO STRAIGHT INTO AN UPDATE, so none may carry a quote character.
// They are worded as readings taken AT THE MOMENT THE CAST WAS DRIVEN, and
// deliberately not as the pre-flight refusals of the same name: a row that says
// `character is moving` was refused before anything was sent, and one that says
// `the character was moving when the cast was driven` sent the packet and
// watched it come to nothing.
namespace CastWall
{
constexpr char const* InFlight = "the character was in flight when the cast was driven";
constexpr char const* Mounted = "the character was on a mount when the cast was driven";
constexpr char const* NotStanding = "the character was sitting when the cast was driven";
constexpr char const* Moving = "the character was moving when the cast was driven";
constexpr char const* OnCooldown = "the spell was on cooldown when the cast was driven";
constexpr char const* OnGlobalCooldown =
    "the global cooldown was still running when the cast was driven";
constexpr char const* AlreadyCasting = "the character was already casting when the cast was driven";
// NOT A WALL, AND IT IS HERE BECAUSE IT IS WHAT THE ROW USED TO GUESS AT. The
// comment beside `casting_after_call` says an absent cast may mean the core
// parked the packet on its spell queue to replay when the global cooldown
// clears, and that this is "indistinguishable from here". It is not: the queue
// is Player::SpellQueue, a public deque of PendingSpellCastRequest carrying the
// spell id, so the executor can look and say so instead of shrugging. A row
// carrying this sentence has not failed; it has not finished, and the verdict
// still comes from where the character is when the window is up.
constexpr char const* Queued =
    "the cast was parked on the core spell queue and had not started yet";
// AND THE HONEST UNKNOWN, WHICH IS NOW A SMALLER ONE. It used to read `nothing
// in front of it was true`, which sent whoever read it next looking for a wall
// this module had never been able to see. It says what it can honestly say: the
// walls this row is able to read were all clear, so the next place to look is a
// wall it cannot read rather than one of these.
constexpr char const* NoneNamed = "the cast did not start and every wall this row can read was clear";
}  // namespace CastWall

// THE `stayed` VERDICT HAS TWO CAUSES AND THE ROW ASSERTED ONLY ONE OF THEM.
// `the cast went out and the character never left` was written into every
// `stayed` row whatever happened, including the eight above, where no cast went
// out at all. A sentence a row cannot have checked is worse than no sentence:
// it sent the operator looking for a failed teleport when the failure was that
// nothing was ever cast.
// AND THEN IT HAD THREE, WHICH IS THE SAME MISTAKE ONE STEP ALONG. `the cast
// never started` is a reading taken in the instant after the packet, and for a
// packet the core copied onto its spell queue that instant is not the story: the
// queue replays it a fraction of a second later, so the row would be asserting
// that nothing was cast when something very probably was. So a parked cast gets
// its own sentence rather than being folded into either of the other two.
constexpr char const* HEARTH_STAYED_CAST_SEEN =
    "the cast went out and the character never left";
constexpr char const* HEARTH_STAYED_NO_CAST =
    "the cast never started and the character never left";
constexpr char const* HEARTH_STAYED_QUEUED =
    "the cast was parked on the core spell queue and the character never left";

// The three are mutually exclusive and asked in the order a row can be sure of
// them: a cast that was SEEN is the one thing here nothing has to infer, a
// parked one is read off the core's own queue, and only what is neither gets the
// sentence that says nothing started.
char const* HearthStayedDetail(bool castWasSeen, bool castWasQueued);



// ------------------------ the party flies, or nobody does (#360, #138, #68) --
//
// THE RULE THIS REPLACES WAS RIGHT ABOUT THE DANGER AND WRONG ABOUT THE
// ALTERNATIVE. Since #138 the group leader refuses to board a taxi while any
// live groupmate on its own map is still on the ground, and the sentence it
// says is a true one: "a party that follows a leader across the sky arrives one
// cliff at a time". That was measured with deaths - the leader flew 4333 yards
// and within three minutes three followers had walked themselves off the far
// side of the terrain he had just flown over.
//
// What the rule assumed is that the thing it was buying was a party walking
// TOGETHER. On the dev realm that assumption was measured false in the same
// breath as the refusal (#360). The leader was 5547 yards from its errand with
// a departure node in reach; the three followers it declined to leave behind
// were between 1500 and 4100 yards behind IT, each on its own catch-up walk,
// each crossing guarded ground alone. The walk it chose instead was 7720 yards,
// five of its seven legs across ground the other side guards, for a party of
// levels 28 to 33 against level 40 guards, on ground those characters had
// already died on. Nobody was walking together. The rule paid the whole price
// of keeping the party together and bought none of it.
//
// SO THE RULE IS KEPT AND THE REMEDY IS INVERTED. The property worth protecting
// is not "the leader stays on the ground", it is "the party ends up in one
// place". A taxi keeps that property perfectly well when everybody is on one:
// every member lands at the SAME arrival node, which is a good deal more
// together than five separate overland walks converging on a moving leader. So
// the question this asks is no longer "is anybody on foot" but "can everybody
// board", and the answer grounds the leader only in the case the old rule was
// actually written for, which is a member that genuinely cannot fly.
//
// WHAT "IN REACH" MEANS, PER MEMBER, DECIDED HERE RATHER THAN LEFT TO A FEELING.
// A member is in reach of a flight when all four of these hold for IT, not for
// the leader:
//
//   1. It has a departure node of its own team where it stands, and a flight
//      master creature standing for that node. Those are two different lookups
//      and they are already required to agree within a hundred yards before
//      this module will issue any flight; nothing about that changes.
//   2. Its walk to that flight master is SHORTER THAN THE WALK THIS MODULE
//      WOULD BOTHER FLYING. That is the definition adopted, and it is chosen
//      because it needs no new number and cannot drift away from the rest of
//      the decision: if getting to the counter is itself a journey long enough
//      to want a flight of its own, it is not "in reach", it is a second
//      errand. The adapter passes the answer, not the yards, for the same
//      reason the counter hold passes the core's own interact gate rather than
//      a distance (#378).
//   3. It holds every node of the route from its own departure node to the
//      SHARED arrival node - the leader's chosen landing - except the departure
//      node itself, which a flight master teaches on arrival exactly as it does
//      for a player. Different members start from different nodes, so this is a
//      different question for each of them and is asked once each.
//   4. It can pay its own fare.
//
// WHO IS NOT ASKED AT ALL, AND WHY EACH ONE IS SAFE TO SKIP. The old rule
// already worked this out and the reasoning is unchanged, so it is carried over
// rather than re-derived: a member on another map cannot follow across one
// (FollowActions.cpp:285); a DEAD one is a ghost walking to its corpse and
// follows nobody (FollowActions.cpp:296-306); one already in the air is not on
// foot. Two more are added here, and they are the only new exemptions:
//
//   * A member ALREADY AT THE ARRIVAL NODE has nothing to cross, so it is not
//     behind anybody. Its own walk is shorter than the one this module would
//     fly, which is the same test as (2) read from the other end.
//   * A member WALKING ITS OWN ERRAND is not following anybody. The danger the
//     whole rule exists to prevent is a follower stepping straight at a master
//     who has crossed a mountain range, and a character aimed at a vendor of
//     its own does not do that: it walks to the vendor. Flying it to the
//     leader's landing would be this module hijacking an errand somebody else
//     wrote, which is a worse fault than the one being fixed. A catch-up walk
//     is NOT its own errand for this purpose - its aim IS the leader's live
//     position, rewritten every time the leader moves on, so a catch-up walker
//     is following in every sense that matters here and is exactly the
//     character #360 measured 4100 yards behind.
//
// WHAT HAPPENS TO A MEMBER THAT IS DEAD, IN COMBAT, OR STRANDED WHEN THE REST
// DEPART - the question #360 asks to have answered explicitly:
//
//   * DEAD: nothing, and it does not stop the party. It follows nobody while it
//     is a ghost, so the danger the whole rule exists to prevent cannot happen
//     to it, and the corpse run plus the catch-up walk that already exist are
//     what bring it back. This is the shipped #138 reading and it is left alone.
//   * IN COMBAT: NOBODY DEPARTS THIS POLL. A fight is over in seconds and the
//     core refuses to board a character in one anyway (Player.cpp:10415), so
//     this is a wait and not a refusal: the verdict is WaitForIt, the errand
//     keeps its walk for now, and the question is asked again on the next poll.
//     Treating it as a refusal would ground a party for the length of one wolf.
//   * STRANDED - no node, no route, no fare: THE PARTY WALKS, and the line says
//     which member and which node. That is the case the old rule was written
//     for and it still gets the old answer. It is also the case worth printing
//     rather than swallowing, because "they will not fly BECAUSE she has never
//     discovered node 17" is a sentence somebody can act on, and "they never
//     fly" is not.
//
// A PERMANENT BLOCKER BEATS A TRANSIENT ONE, wherever they land in the roster.
// If one member is in combat and another holds no node at all, waiting for the
// fight to end achieves nothing except a later refusal, so the verdict is Walk.
// Scanning every member before deciding, rather than returning on the first
// thing found, is what makes that true regardless of the order the caller
// happens to hand them over in - and it is what lets the report name the member
// that actually matters rather than the first one that was awkward.
enum class PartyFlightBlock : std::uint8_t
{
    // Nothing is in this member's way.
    None,
    // TRANSIENT. The core will not board a character in combat and the fight
    // will be over shortly; ask again rather than walking the party.
    InCombat,
    // This module does not steer this character at all: no live bot AI, or one
    // that does not carry the strategy every aimed errand runs through. It is
    // on foot, it will follow, and nothing here can put it on a taxi. Asked
    // first of the permanent blockers because it is the one that makes every
    // other question about that member meaningless.
    NotSteerable,
    // There is no taxi node of this character's own team where it stands.
    NoDepartureNode,
    // There is a node, and no flight master this character could walk to
    // without that walk becoming a journey of its own - see (2) above.
    MasterOutOfReach,
    // The route from this member's node to the shared landing runs through a
    // node it has never visited. `blockedNode` carries which one, because that
    // number is the entire actionable content of this refusal.
    UndiscoveredNode,
    // The graph has no route at all from this member's node to the landing.
    NoRoute,
    // It cannot pay its own fare.
    TooPoor,
};

// One clause, in the module's own voice, for the report the caller writes. No
// member name and no node id: those are the caller's to interpolate, and a
// sentence that carried them would have to be built here out of pieces this
// file has no business holding.
char const* PartyFlightBlockWord(PartyFlightBlock block);

// One member of the party as the adapter read it out of the world. Every field
// is an ANSWER and not a measurement, for the reason the long comment gives:
// the yards, the npcflags and the taximask bits belong on the other side of
// this seam, and a pure decision that started comparing distances would start
// disagreeing with the executor that actually issues the flight.
struct PartyFlightMember
{
    std::string name;
    // The group leader, which is the character carrying the errand. Exactly one
    // member should carry this; a roster that carries none is a caller bug and
    // is refused rather than guessed at.
    bool leader{false};
    // Not on the leader's map: cannot follow across one, so not behind it.
    bool onSameMap{true};
    // Alive: a ghost follows nobody.
    bool alive{true};
    // Already on a taxi: not on foot.
    bool inFlight{false};
    // In combat right now.
    bool inCombat{false};
    // Already near enough to the shared arrival node that it has nothing to
    // cross - see the exemptions above.
    bool atArrival{false};
    // Following the leader, rather than walking an errand of its own. False for
    // a member carrying its own aim somewhere else, which this never puts on a
    // taxi and never grounds the party for - see the exemptions above.
    bool followingTheLeader{true};
    // Whether this module steers this character at all - the same predicate
    // every other aimed errand is gated on, asked of the member rather than
    // assumed from the leader.
    bool steerable{true};
    // The four halves of "in reach", each answered by the adapter.
    bool hasDepartureNode{false};
    bool masterInReach{false};
    bool routeKnown{false};
    bool canPayFare{false};
    // Which node it would board at, for the report. Zero when it has none.
    std::uint32_t departureNode{0};
    // The node on its route it has never discovered, when `routeKnown` is false
    // because of one. Zero when the route is missing for any other reason,
    // which is how the two are told apart.
    std::uint32_t undiscoveredNode{0};
};

enum class PartyFlightVerdict : std::uint8_t
{
    // Everybody who is behind the leader can board. Issue a flight for each
    // name in `boarding`, all to the same arrival node.
    Fly,
    // Nobody is permanently stuck and somebody is momentarily busy. Do not
    // board anyone, do not spend the errand's flight budget, and ask again next
    // poll. The walk carries on meanwhile, which is what it was doing anyway.
    WaitForIt,
    // Somebody cannot fly at all. The party walks, exactly as it has since
    // #138, and `blockedBy` plus `block` say who and why.
    Walk,
};

char const* PartyFlightVerdictWord(PartyFlightVerdict verdict);

struct PartyFlightPlan
{
    PartyFlightVerdict verdict{PartyFlightVerdict::Walk};
    // Who departs, in roster order, when the verdict is Fly. The leader is
    // always one of them: a plan that flew everybody except the character
    // carrying the errand would be the #138 failure with the roles swapped.
    // Empty for every other verdict.
    std::vector<std::string> boarding;
    // The member the verdict is about, for Walk and WaitForIt. Empty for Fly.
    std::string blockedBy;
    PartyFlightBlock block{PartyFlightBlock::None};
    // The node `blockedBy` has never discovered, when that is what stopped it.
    std::uint32_t blockedNode{0};
};

// `members` is the whole group including the leader, as read this poll. The
// arrival node is not passed because this makes no decision about WHERE to
// land: the caller has already chosen the landing the leader can reach, and
// `routeKnown` is each member's answer about that same landing.
PartyFlightPlan PlanPartyFlight(std::vector<PartyFlightMember> const& members);

}  // namespace OverseerDecisions

#endif  // MOD_OVERSEER_DECISIONS_H
