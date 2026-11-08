#ifndef OVERSEER_STALE_COMBAT_H
#define OVERSEER_STALE_COMBAT_H

// A COMBAT FLAG THAT NEVER CLEARS (#1390).
//
// Every walk, crossing and vendor errand refuses a character in combat, and
// that is right while something is fighting it. Measured on the dev realm: one
// quester was refused `character is in combat` on every errand for more than
// 29 hours (135 spawn walks, 38 crossings and 19 vendor walks in under seven
// hours of one log), across two relogs and a worldserver restart, standing at
// one position the whole time with no fight, death or kill line in between.
// Nothing in the module noticed, so the character could not sell, quest or
// cross for a day and a half.
//
// THE RULE. A living character in the open world that has been in combat with
// no hostile in reach for STALE_COMBAT_RELEASE_SECONDS is released the way a
// player leaves combat: the core's combat stop, which ends every combat
// reference, drops the attackers and clears the flag. "A hostile in reach" is a
// unit that is attacking the character, or holds a combat reference to it,
// that is alive, on the same map and within STALE_COMBAT_REACH_YARDS; a
// creature that is evading, cannot reach its target or fights nobody does not
// count (#1399: a family member frozen in combat for 13 hours held its
// campaign's reset and entry the whole time). A live
// fight always has one, so a real fight is never cut short; and if the release
// was wrong, the next swing puts the character straight back in combat, which
// is why the release is safe to take without asking anybody.
//
// THE CLOCK RESTARTS ON ANY HOSTILE IN REACH and on leaving combat, so only an
// unbroken stretch of empty combat counts. Ten minutes is far longer than any
// honest gap in a fight (a creature fleeing and coming back, a caster out of
// line of sight) and short against the hours this was measured at.
//
// THE OPEN WORLD ONLY. A dungeon run has its own busy ceiling for a member
// whose flag never clears (DUNGEON_CLEAR_BUSY_CEILING_SECONDS, #382), and a
// battleground or arena keeps combat on purpose; both are on instanced maps.
//
// AND THE HOLD IS SAID WHEN IT IS NOT RELEASED. A character in combat for
// STALE_COMBAT_SAY_HELD_SECONDS with a hostile in reach the whole time is not
// stale by this rule, but it is just as stuck. It gets one line every
// STALE_COMBAT_SAY_HELD_SECONDS naming the nearest hostile and its distance, so
// the next live check can tell that case from this one.
//
// A STANDOFF IS NOT A FIGHT EITHER (#1411). A creature a few yards away that
// holds a combat reference, has a victim and is not evading counts as a
// hostile in reach, and that is right while either side is hurting the other.
// Measured on the dev realm after #1394 went live: a family member stood in
// combat at one spot for hours, at full health, with a creature five yards off
// and six yards above it, both at full health, neither ever landing a blow
// (the "held by ... not released" line every thirty minutes, and the same line
// for four other characters in the same hour). A standoff is a stretch in
// which the character and every hostile in reach stay at full health and the
// character does not move more than STALE_COMBAT_STANDOFF_MOVE_YARDS from
// where the stretch began. After STALE_COMBAT_STANDOFF_SECONDS of it, the
// character gets the same combat stop and each creature in the standoff is
// sent home. Any damage on either side, or a move, ends the stretch, so a real
// fight is never cut short: a fight hurts somebody within seconds.
//
// No AzerothCore include: tests/test_stale_combat.cpp compiles this alone.

#include <ctime>

namespace OverseerDecisions
{

constexpr time_t STALE_COMBAT_RELEASE_SECONDS = 10 * 60;
constexpr float STALE_COMBAT_REACH_YARDS = 40.0f;
constexpr time_t STALE_COMBAT_SAY_HELD_SECONDS = 30 * 60;
constexpr time_t STALE_COMBAT_STANDOFF_SECONDS = 15 * 60;
constexpr float STALE_COMBAT_STANDOFF_MOVE_YARDS = 5.0f;

// What the sweep reads about one character on one poll.
struct StaleCombatFacts
{
    bool alive = false;
    bool inCombat = false;
    // Map::Instanceable(): a dungeon, raid, battleground or arena.
    bool onInstanceMap = false;
    // Attackers plus combat-reference holders that are alive, on the same map
    // and within STALE_COMBAT_REACH_YARDS.
    unsigned hostilesInReach = 0;
    // With hostiles in reach: the character and every one of them are at full
    // health, and the character is within STALE_COMBAT_STANDOFF_MOVE_YARDS of
    // where the stretch began (#1411). Nobody is hurting anybody.
    bool standoff = false;
    time_t now = 0;
};

enum class StaleCombatVerdict
{
    NotInCombat,     // forget the character
    OutOfScope,      // an instanced map: not this rule's to judge
    HeldByHostile,   // a hostile is in reach: a real fight, the clock restarts
    Watching,        // empty combat, not yet long enough
    Release,         // empty combat for the whole bound: stop combat now
    StandoffWatching,  // hostiles in reach, nobody hurt, not yet long enough
    ReleaseStandoff,   // a standoff for the whole bound: stop combat, send
                       // the standoff creatures home (#1411)
};

struct StaleCombatStep
{
    StaleCombatVerdict verdict = StaleCombatVerdict::NotInCombat;
    // When the empty stretch began, to carry to the next poll; 0 = none.
    time_t emptySince = 0;
    // When the first standoff poll of this stretch was, to carry to the next
    // poll; 0 = none yet. The standoff bound counts from here, not from the
    // start of the stretch: empty combat that a full-health creature walks
    // into has not been a standoff for the empty minutes before it came.
    time_t standoffSince = 0;
    // How long the empty stretch lasted, for the release line (for a
    // standoff, how long the standoff lasted).
    time_t heldSeconds = 0;
};

// Whether one poll reads as a standoff (#1411): hostiles in reach, the
// character and every one of them at full health, and the character no more
// than STALE_COMBAT_STANDOFF_MOVE_YARDS from where its no-fight stretch began
// (a negative distance: no stretch yet, so nothing to have moved from). A blow
// on either side or a move makes it a fight, which restarts the clock.
inline bool IsStandoff(unsigned hostilesInReach, bool everyoneFull, float yardsFromAnchor)
{
    return hostilesInReach > 0 && everyoneFull &&
           yardsFromAnchor <= STALE_COMBAT_STANDOFF_MOVE_YARDS;
}

// One poll's verdict, given when the empty stretch began (0 = it had not) and
// when its first standoff poll was (0 = none yet).
inline StaleCombatStep DecideStaleCombat(StaleCombatFacts const& f, time_t emptySince,
                                         time_t standoffSince)
{
    StaleCombatStep step;
    if (!f.alive || !f.inCombat)
        return step;
    if (f.onInstanceMap)
    {
        step.verdict = StaleCombatVerdict::OutOfScope;
        return step;
    }
    bool const standoff = f.hostilesInReach > 0;
    if (standoff && !f.standoff)
    {
        step.verdict = StaleCombatVerdict::HeldByHostile;
        return step;
    }
    StaleCombatVerdict const watching =
        standoff ? StaleCombatVerdict::StandoffWatching : StaleCombatVerdict::Watching;
    // A clock from the future (a restart, a clock step) restarts rather than
    // releasing on a negative or huge difference.
    if (emptySince <= 0 || emptySince > f.now)
    {
        step.verdict = watching;
        step.emptySince = f.now;
        step.standoffSince = standoff ? f.now : 0;
        return step;
    }
    // The standoff's own start: carried through a poll where the creatures
    // drop out of reach, begun at the first standoff poll otherwise.
    if (standoffSince < emptySince || standoffSince > f.now)
        standoffSince = 0;
    if (standoff && standoffSince == 0)
        standoffSince = f.now;
    time_t const held = standoff ? f.now - standoffSince : f.now - emptySince;
    time_t const bound = standoff ? STALE_COMBAT_STANDOFF_SECONDS : STALE_COMBAT_RELEASE_SECONDS;
    if (held >= bound)
    {
        step.verdict = standoff ? StaleCombatVerdict::ReleaseStandoff : StaleCombatVerdict::Release;
        step.heldSeconds = held;
        return step;
    }
    step.verdict = watching;
    step.emptySince = emptySince;
    step.standoffSince = standoffSince;
    return step;
}

// Whether a character held in combat by a hostile in reach is said this poll:
// in combat (for any reason) since `combatSince`, last said at `lastSaid`
// (0 = never). Once the hold reaches STALE_COMBAT_SAY_HELD_SECONDS, and then at
// most once per that interval.
inline bool SayStaleCombatHeld(time_t now, time_t combatSince, time_t lastSaid)
{
    if (combatSince <= 0 || combatSince > now)
        return false;
    if (now - combatSince < STALE_COMBAT_SAY_HELD_SECONDS)
        return false;
    return lastSaid <= 0 || lastSaid > now || now - lastSaid >= STALE_COMBAT_SAY_HELD_SECONDS;
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_STALE_COMBAT_H
