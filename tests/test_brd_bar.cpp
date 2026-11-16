/*
 * The Grim Guzzler's patrons are held at REACT_DEFENSIVE while a family member
 * is in the instance and the bar is hostile, and the exit walk's fights are
 * logged with enough to say who started them.
 *
 * Plugger Spazzring's death script sets every patron to faction 14 and only a
 * respawn restores it, so every walk through the bar after the clear crossed
 * two proximity-aggro formations. The rules live in src/overseer_brd_bar.h;
 * the adapter's wiring is pinned in the source because it needs a worldserver.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_brd_bar.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::ostringstream contents;
    contents << source.rdbuf();
    return contents.str();
}

BarPatronFacts Patron(std::uint32_t entry = BRD_GRIM_PATRON_ENTRY)
{
    BarPatronFacts f;
    f.entry = entry;
    f.alive = true;
    return f;
}

void TheEntries()
{
    Expect(BRD_PLUGGER_ENTRY == 9499, "Plugger Spazzring is 9499");
    Expect(IsBarPatron(9545) && IsBarPatron(9547) && IsBarPatron(9554),
           "the Grim, Guzzling and Hammered Patrons are the bar");
    Expect(!IsBarPatron(9499) && !IsBarPatron(0) && !IsBarPatron(9546),
           "nothing else is a patron, Plugger included");
}

void WhenTheBarIsArmed()
{
    Expect(!BarArmed(false, 0), "a bar nobody has touched is not armed");
    Expect(BarArmed(true, 0), "Plugger dead arms it before any faction is read");
    Expect(BarArmed(false, 3), "a patron off its template faction arms it");
}

void TheHold()
{
    Expect(BarPatronAction(true, Patron()) == BarAction::Hold,
           "an aggressive patron is held while a member is in the instance");
    Expect(BarPatronAction(true, Patron(BRD_GUZZLING_PATRON_ENTRY)) == BarAction::Hold &&
               BarPatronAction(true, Patron(BRD_HAMMERED_PATRON_ENTRY)) == BarAction::Hold,
           "all three patron kinds are held");
    Expect(BarPatronAction(false, Patron()) == BarAction::None,
           "with nobody walking nothing is held");

    BarPatronFacts f = Patron();
    f.alive = false;
    Expect(BarPatronAction(true, f) == BarAction::None, "a dead patron is left alone");
    f = Patron();
    f.summon = true;
    Expect(BarPatronAction(true, f) == BarAction::None, "a summon or pet is never held");
    f = Patron();
    f.engaged = true;
    Expect(BarPatronAction(true, f) == BarAction::None,
           "a patron already fighting is left as it is");
    f = Patron();
    f.reactState = BAR_REACT_PASSIVE;
    Expect(BarPatronAction(true, f) == BarAction::None,
           "a patron a script put at PASSIVE is none of ours");
    f = Patron();
    f.reactState = BAR_REACT_DEFENSIVE;
    Expect(BarPatronAction(true, f) == BarAction::None,
           "an already defensive patron is not re-held");
    Expect(BarPatronAction(true, Patron(BRD_PLUGGER_ENTRY)) == BarAction::None,
           "Plugger himself is never held");
}

void TheKillArmsTheHoldAtOnce()
{
    Expect(BarHoldOnKill(BRD_PLUGGER_ENTRY), "Plugger's death arms the hold");
    Expect(!BarHoldOnKill(BRD_GRIM_PATRON_ENTRY) && !BarHoldOnKill(0) && !BarHoldOnKill(9500),
           "nothing else's death does");
    // The boundary the review named: the tick Plugger dies the patrons are hostile
    // but nobody is fighting yet, so every one is held; one that is already
    // engaged is not, which is exactly why the hold cannot wait for a poll.
    Expect(BarArmed(true, 0) && BarPatronAction(true, Patron()) == BarAction::Hold,
           "in the tick of the kill an idle patron is held");
    BarPatronFacts engaged = Patron();
    engaged.engaged = true;
    Expect(BarPatronAction(true, engaged) == BarAction::None,
           "a patron that has already engaged is skipped, which is the window to close");
}

void TheRelease()
{
    BarPatronFacts f = Patron();
    f.held = true;
    f.reactState = BAR_REACT_DEFENSIVE;
    Expect(BarPatronAction(true, f) == BarAction::None,
           "a held patron stays held while a member walks");
    Expect(BarPatronAction(false, f) == BarAction::Release,
           "it is released when no member is in the instance");
    f.engaged = true;
    Expect(BarPatronAction(false, f) == BarAction::Release,
           "a held patron is released even mid-fight");
    Expect(BarReleaseWrites(BAR_REACT_DEFENSIVE), "a still-defensive patron is put back");
    Expect(!BarReleaseWrites(BAR_REACT_PASSIVE) && !BarReleaseWrites(BAR_REACT_AGGRESSIVE),
           "a patron something else moved is not written over");
}

void TheLogLine()
{
    std::string const line = BarAttackerLine("Zork", 9545, 1234, 14, 35, 12.34f, false, 0);
    Expect(line.find("'Zork'") != std::string::npos, "the member is named");
    Expect(line.find("entry 9545") != std::string::npos &&
               line.find("guid 1234") != std::string::npos,
           "the attacker's entry and guid are there");
    Expect(line.find("faction 14 template faction 35 (CHANGED)") != std::string::npos,
           "current faction against the template's, flagged when they differ");
    Expect(line.find("12.3 yards") != std::string::npos, "the distance is there");
    Expect(line.find("casting now no") != std::string::npos &&
               line.find("casts this fight 0") != std::string::npos,
           "whether the member cast anything is there");
    Expect(BarAttackerLine("Zork", 9545, 1, 35, 35, 1.f, true, 2).find("(unchanged)") !=
               std::string::npos,
           "an attacker on its own faction is said to be unchanged");
}

void TheAdapterWiresIt()
{
    std::string const src = Read("src/mod_overseer.cpp");
    Expect(!src.empty(), "src/mod_overseer.cpp is readable from the repo root");
    Expect(src.find("#include \"overseer_brd_bar.h\"") != std::string::npos,
           "the header is included");

    // THE PASS, shared by the sweep and the kill hook.
    std::size_t const pass = src.find("\nBrdBarPass ApplyBrdBar(Map* map, bool pluggerKnownDead)\n{");
    Expect(pass != std::string::npos, "the adapter has the shared ApplyBrdBar pass");
    std::size_t const passEnd = src.find("\n}\n", pass);
    auto inPass = [&](char const* needle)
    {
        std::size_t const at = src.find(needle, pass);
        return pass != std::string::npos && at != std::string::npos && at < passEnd;
    };
    Expect(inPass("OverseerDecisions::BarArmed(") && inPass("BRD_PLUGGER_ENTRY"),
           "it asks whether the bar is armed, from Plugger and the patrons' factions");
    Expect(inPass("GetCreatureTemplate()->faction") && inPass("GetFaction()"),
           "it compares the current faction with the template's");
    Expect(inPass("OnRoster(") && inPass("IsAlive()"), "it looks for a live family member");
    Expect(inPass("OverseerDecisions::BarPatronAction("), "the rules decide each creature");
    Expect(inPass("SetReactState(REACT_DEFENSIVE)") && inPass("SetReactState(REACT_AGGRESSIVE)"),
           "it holds with REACT_DEFENSIVE and releases to REACT_AGGRESSIVE");
    Expect(inPass("g_brdBarHeld"), "what it held is remembered by guid");
    Expect(inPass("pluggerKnownDead"),
           "the kill hook's own knowledge arms it, without waiting for the corpse to read dead");
    Expect(!inPass("SetFaction(") && !inPass("DespawnOrUnsummon(") && !inPass("KillSelf(") &&
               !inPass("setDeathState("),
           "it never kills, despawns or re-factions a patron");

    // THE SWEEP is the backstop and the release.
    std::size_t const drive = src.find("    void DriveBrdBar()");
    Expect(drive != std::string::npos, "the adapter has DriveBrdBar");
    std::size_t const end = src.find("\n    }\n", drive);
    auto within = [&](char const* needle)
    {
        std::size_t const at = src.find(needle, drive);
        return drive != std::string::npos && at != std::string::npos && at < end;
    };
    Expect(within("sMapMgr->DoForAllMapsWithMapId(OverseerDecisions::BRD_WING_MAP_ID"),
           "the sweep walks every instance of map 230");
    Expect(within("ApplyBrdBar(map, false)"), "the sweep runs the shared pass");

    // THE TIMING BOUNDARY: the hold is armed in the tick Plugger dies.
    std::size_t const hold = src.find("\nvoid HoldBrdBarOnKill(Creature* killed)\n{");
    Expect(hold != std::string::npos, "the adapter has HoldBrdBarOnKill");
    std::size_t const holdEnd = src.find("\n}\n", hold);
    auto inHold = [&](char const* needle)
    {
        std::size_t const at = src.find(needle, hold);
        return hold != std::string::npos && at != std::string::npos && at < holdEnd;
    };
    Expect(inHold("OverseerDecisions::BarHoldOnKill(killed->GetEntry())") &&
               inHold("ApplyBrdBar(killed->GetMap(), true)"),
           "a Plugger kill runs the pass at once, knowing he is dead");
    for (char const* signature :
         {"void OnPlayerCreatureKill(Player* killer, Creature* killed) override\n    {\n",
          "void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override\n    {\n"})
    {
        std::size_t const at = src.find(signature);
        Expect(at != std::string::npos &&
                   src.compare(at + std::string(signature).size(), 40,
                               "        HoldBrdBarOnKill(killed);\n        Note", 0, 40) == 0,
               "each creature-kill hook arms the hold as its FIRST statement, ahead of the "
               "loot and hunt bookkeeping");
    }
    Expect(src.find("PLAYERHOOK_ON_CREATURE_KILL,") != std::string::npos &&
               src.find("PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,") != std::string::npos,
           "the event script is subscribed to both kill hooks");

    // The drive runs on the engagement poll, with the other per-poll drives.
    std::size_t const poll = src.find("DriveStaleCombat();");
    std::size_t const call = src.find("DriveBrdBar();", poll);
    Expect(poll != std::string::npos && call != std::string::npos && call - poll < 600,
           "the engagement poll calls it beside DriveStaleCombat");

    // The combat log.
    std::size_t const hook =
        src.find("void OnPlayerEnterCombat(Player* player, Unit* enemy) override");
    Expect(src.find("PLAYERHOOK_ON_PLAYER_ENTER_COMBAT") != std::string::npos &&
               hook != std::string::npos,
           "the event script hooks a player entering combat");
    std::size_t const hookEnd = src.find("\n    }\n", hook);
    auto inHook = [&](char const* needle)
    {
        std::size_t const at = src.find(needle, hook);
        return hook != std::string::npos && at != std::string::npos && at < hookEnd;
    };
    Expect(inHook("BRD_WING_MAP_ID") && inHook("IsLeaveInstanceWalker("),
           "it only speaks on map 230 during a LeaveInstance escort");
    Expect(inHook("getAttackers()") && inHook("OverseerDecisions::BarAttackerLine("),
           "it logs every attacker through the shared line");
    Expect(src.find("PLAYERHOOK_ON_SPELL_CAST") != std::string::npos &&
               src.find("PLAYERHOOK_ON_PLAYER_LEAVE_COMBAT") != std::string::npos,
           "spell casts are counted for the fight, and the fight ends on leaving combat");
    Expect(src.find("SetLeaveInstanceWalkers(") != std::string::npos,
           "the sweep publishes who is being walked out");
}

}  // namespace

int main()
{
    TheEntries();
    WhenTheBarIsArmed();
    TheHold();
    TheKillArmsTheHoldAtOnce();
    TheRelease();
    TheLogLine();
    TheAdapterWiresIt();
    if (failures)
    {
        std::fprintf(stderr, "%d Grim Guzzler check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_brd_bar: ok\n");
    return 0;
}
