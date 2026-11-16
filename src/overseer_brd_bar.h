#ifndef OVERSEER_BRD_BAR_H
#define OVERSEER_BRD_BAR_H

// BLACKROCK DEPTHS: THE GRIM GUZZLER'S PATRONS ARE HELD BACK AFTER PLUGGER
// SPAZZRING DIES, SO NOBODY IS WALKED THROUGH THEM LIVE.
//
// Killing Plugger Spazzring (9499) runs his death script, which sets every
// Grim (9545), Guzzling (9547) and Hammered (9554) Patron on the map to the
// hostile faction 14. Only the patron's own respawn puts the faction back;
// leaving combat does not. The dungeon clear kills Plugger as a required
// encounter, so every walk through the Grim Guzzler afterwards, the run's own
// exit walk included, crosses two formations (seven patrons, nineteen patrons)
// that aggro by proximity, and the formation's group assist pulls all of each
// one at a time on whoever is walking.
//
// THE FIX IS A REACT STATE, NOT A KILL. While the bar is armed (Plugger is
// dead in the instance, or any patron's current faction is not its template
// faction) and a family member is alive in that instance, every live patron
// is set to REACT_DEFENSIVE. A defensive creature does not aggro on
// proximity and does not answer a call for help, and it still fights back the
// moment it is attacked, which is correct. The moment no member is in the
// instance (or the bar is not armed) the patrons this module held go back to
// REACT_AGGRESSIVE. Nothing is killed, despawned or given a different
// faction, and nothing is granted.
//
// The same shape mod-dungeon-clear's DcSocialQuarantine uses: only ever move
// a creature between AGGRESSIVE and DEFENSIVE, never touch an engaged one, and
// never a summon. Unlike it, the creatures this module held are remembered by
// GUID, so a patron whose own script had set another react state is never
// stomped on release.
//
// No AzerothCore include: tests/test_brd_bar.cpp compiles this alone.

#include <cstdint>
#include <cstdio>
#include <string>

namespace OverseerDecisions
{

constexpr std::uint32_t BRD_PLUGGER_ENTRY = 9499;
constexpr std::uint32_t BRD_GRIM_PATRON_ENTRY = 9545;
constexpr std::uint32_t BRD_GUZZLING_PATRON_ENTRY = 9547;
constexpr std::uint32_t BRD_HAMMERED_PATRON_ENTRY = 9554;

// ReactStates in the core: REACT_PASSIVE 0, REACT_DEFENSIVE 1, REACT_AGGRESSIVE 2.
constexpr int BAR_REACT_PASSIVE = 0;
constexpr int BAR_REACT_DEFENSIVE = 1;
constexpr int BAR_REACT_AGGRESSIVE = 2;

inline bool IsBarPatron(std::uint32_t entry)
{
    return entry == BRD_GRIM_PATRON_ENTRY || entry == BRD_GUZZLING_PATRON_ENTRY ||
           entry == BRD_HAMMERED_PATRON_ENTRY;
}

// Is the bar hostile in this instance? Plugger dead is the cause; a patron off
// its template faction is the symptom, and either one is enough because the
// faction is only ever put back by a respawn.
inline bool BarArmed(bool pluggerDead, unsigned patronsOffTemplateFaction)
{
    return pluggerDead || patronsOffTemplateFaction > 0;
}

// A KILL THAT ARMS THE HOLD AT ONCE. Plugger's death makes every patron hostile
// in the same breath, so the hold cannot wait for a poll: the kill hook asks this
// and runs the pass in the same tick, before any walker can reach a formation.
inline bool BarHoldOnKill(std::uint32_t killedEntry)
{
    return killedEntry == BRD_PLUGGER_ENTRY;
}

enum class BarAction
{
    None,
    Hold,     // set REACT_DEFENSIVE and remember the creature
    Release   // restore REACT_AGGRESSIVE if it is still ours, and forget the creature
};

struct BarPatronFacts
{
    std::uint32_t entry{0};
    bool alive{false};
    bool summon{false};   // a summon or a pet is never a candidate
    bool engaged{false};  // already in a fight
    int reactState{BAR_REACT_AGGRESSIVE};
    bool held{false};     // this module put it at DEFENSIVE
};

// What to do with one creature this poll. `hold` is "the bar is armed AND a
// family member is alive in this instance".
inline BarAction BarPatronAction(bool hold, BarPatronFacts const& f)
{
    if (f.held && !hold)
        return BarAction::Release;
    if (!hold)
        return BarAction::None;
    // A HELD PATRON FALLS THROUGH, NOT OUT: a patron that respawns while the bar
    // is armed has its react state reset to aggressive by the core, and that is
    // read here like any aggressive patron, so the hold is reasserted.
    if (!IsBarPatron(f.entry) || !f.alive || f.summon)
        return BarAction::None;
    // Who joins a fight is the concern, not who is in one.
    if (f.engaged)
        return BarAction::None;
    // Only an AGGRESSIVE creature is ours to move; PASSIVE belongs to a script.
    return f.reactState == BAR_REACT_AGGRESSIVE ? BarAction::Hold : BarAction::None;
}

// Whether a release should write the react state back: only if nothing else has
// changed it since.
inline bool BarReleaseWrites(int reactState)
{
    return reactState == BAR_REACT_DEFENSIVE;
}

// THE COMBAT LOG LINE (an open question this settles): who attacked a family
// member inside the Blackrock Depths during an exit walk, what faction each
// attacker is on now against the faction its template gives it, how far it was,
// and whether the member had cast anything.
inline std::string BarAttackerLine(std::string const& member, std::uint32_t entry,
                                   std::uint64_t guid, std::uint32_t faction,
                                   std::uint32_t templateFaction, float distance,
                                   bool memberCasting, unsigned memberCasts)
{
    char buffer[384];
    std::snprintf(buffer, sizeof(buffer),
                  "overseer: '%s' entered combat on the exit walk in the Blackrock Depths: "
                  "attacker entry %u guid %llu faction %u template faction %u (%s) at %.1f "
                  "yards; member casting now %s, casts this fight %u",
                  member.c_str(), static_cast<unsigned>(entry),
                  static_cast<unsigned long long>(guid), static_cast<unsigned>(faction),
                  static_cast<unsigned>(templateFaction),
                  faction == templateFaction ? "unchanged" : "CHANGED",
                  static_cast<double>(distance), memberCasting ? "yes" : "no", memberCasts);
    return std::string(buffer);
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_BRD_BAR_H
