/*
 * A guild death line says what was on the member, what was moving it and
 * whether its pet was out (mod-overseer#1468).
 *
 * The live failure this pins. Over one day the dev realm logged 3974 guild
 * deaths, 52 percent warlocks, and every line read "with 0 attacker(s) on it"
 * while every recorded-death line read "driven by unknown, movement ''". The
 * core empties the attacker list, clears the motion master and dismisses
 * nothing it can still answer for before any death hook runs, so the death
 * hook read blind. The damage hook already keeps each hit with the state it
 * found (#1416); this reads three more facts off the same hit and says them.
 *
 * Compiled against src/overseer_decisions.cpp, and reads src/mod_overseer.cpp
 * to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

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

// A level 43 warlock fighting one even mob, 100 percent at 100.0s, and dead by 106.0s.
DeathHit Hit(std::int64_t atMs, std::uint32_t health, std::uint32_t mana, std::uint32_t attackers)
{
    DeathHit hit;
    hit.atMs = atMs;
    hit.amount = 50;
    hit.source = "Dustbelcher Shaman";
    hit.healthBefore = health;
    hit.maxHealth = 1000;
    hit.manaBefore = mana;
    hit.maxMana = 1000;
    hit.inCombat = true;
    hit.enemiesEngaged = 1;
    hit.attackers = attackers;
    hit.movement = MoveGenerator::Chase;
    hit.controller = "a playerbot on 'new rpg'";
    hit.pet = "none summoned";
    return hit;
}

DeathHitRing Warlock()
{
    DeathHitRing ring;
    RememberDeathHit(ring, Hit(100000, 1000, 800, 1));
    RememberDeathHit(ring, Hit(104000, 600, 300, 1));
    RememberDeathHit(ring, Hit(106000, 120, 30, 2));
    return ring;
}

void TheAccountCarriesTheTelemetry()
{
    DeathHitAccount const a = AccountForDeathHits(Warlock(), 106400);
    Check("the attackers are the newest hit's, not zero", a.attackers == 2);
    Check("the mover is the newest hit's", a.movement == MoveGenerator::Chase);
    Check("the controller is the newest hit's", a.controller == "a playerbot on 'new rpg'");
    Check("the pet is the newest hit's", a.pet == "none summoned");
    Check("an earlier reading comes from the oldest hit inside the window",
          a.earlierSeen && a.earlierSeconds == 6 && a.earlierHealth == 1000 && a.earlierMana == 800);

    DeathHitRing lone;
    RememberDeathHit(lone, Hit(100000, 90, 10, 1));
    DeathHitAccount const b = AccountForDeathHits(lone, 100500);
    Check("one hit has no earlier reading of its own", !b.earlierSeen);

    DeathHitRing slow;
    RememberDeathHit(slow, Hit(100000, 1000, 800, 1));
    RememberDeathHit(slow, Hit(125000, 100, 30, 1));
    DeathHitAccount const c = AccountForDeathHits(slow, 125400);
    Check("a hit older than the earlier window is not 'a few seconds earlier'", !c.earlierSeen);
}

void TheWordsAreHonest()
{
    DeathHitAccount const a = AccountForDeathHits(Warlock(), 106400);
    Check("a sampled count is said as a number", GuildDeathAttackers(a) == "2");
    Check("an unsampled count is never said as 0", GuildDeathAttackers(DeathHitAccount{}) == "unsampled");
    Check("the telemetry says mover, movement, pet, health and mana",
          GuildDeathTelemetry(a) ==
              ", moved by a playerbot on 'new rpg' (movement 'chase'), pet none summoned, health 12% "
              "at the last hit (100% 6s before), mana 3% (80% 6s before)");
    Check("an unsampled death says so rather than inventing a mover",
          GuildDeathTelemetry(DeathHitAccount{}).find("unsampled") != std::string::npos &&
              GuildDeathTelemetry(DeathHitAccount{}).find("chase") == std::string::npos);

    DeathHit warrior = Hit(100000, 400, 0, 1);
    warrior.maxMana = 0;
    DeathHitRing ring;
    RememberDeathHit(ring, warrior);
    Check("a member without a mana bar says so",
          GuildDeathTelemetry(AccountForDeathHits(ring, 100200)).find("mana no bar") != std::string::npos);
}

void ThePetAndTheController()
{
    PetFacts pet;
    Check("a warrior has no pet to ask about", NameThePetState(pet) == "not a pet class");
    pet.petClass = true;
    Check("a warlock with nothing out", NameThePetState(pet) == "none summoned");
    pet.unsummoned = true;
    Check("a warlock who put it away", NameThePetState(pet) == "put away");
    pet.out = true;
    pet.alive = true;
    Check("a living pet is out", NameThePetState(pet) == "out");
    pet.alive = false;
    Check("a dead pet is out and dead", NameThePetState(pet) == "out and dead");

    ControllerFacts c;
    Check("no bot is a player", NameTheController(c) == "a player");
    c.bot = true;
    Check("a bot with no movement strategy says so",
          NameTheController(c) == "a playerbot with no movement strategy");
    c.newRpg = true;
    Check("a bot on new rpg", NameTheController(c) == "a playerbot on 'new rpg'");
    c.follow = true;
    Check("follow outranks new rpg", NameTheController(c) == "a playerbot on 'follow'");
    c.stay = true;
    Check("stay outranks follow", NameTheController(c) == "a playerbot on 'stay'");
}

// ---------------------------------------------------------- the wiring --

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const begin = source.find(from);
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
}

void TheAdapterIsWired()
{
    std::string const source = Read("src/mod_overseer.cpp");
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    std::string const note = Between(source, "static void NoteDeathHit(", "static void SweepDeathHits(");
    Check("every hit counts who has the member as its target",
          note.find("hit.attackers = static_cast<uint32>(names.size());") != std::string::npos &&
              note.find("who->GetVictim() == hurt") != std::string::npos);
    Check("every hit reads what the motion master is running",
          note.find("hit.movement = FoldMovementGenerator(motion->GetCurrentMovementGeneratorType());") !=
              std::string::npos);
    Check("every hit reads the controller and the pet",
          note.find("GET_PLAYERBOT_AI(hurt)") != std::string::npos &&
              note.find("hurt->GetPet()") != std::string::npos &&
              note.find("OverseerDecisions::NameThePetState(pet)") != std::string::npos &&
              note.find("OverseerDecisions::NameTheController(controller)") != std::string::npos);

    std::string const guild = Between(source, "static void LogGuildDeath(", "// The level-up record");
    Check("the guild death line no longer reads the emptied attacker list",
          !guild.empty() && guild.find("getAttackers()") == std::string::npos);
    Check("the guild death line reads the hook's sample and says it",
          guild.find("PeekDeathHits(killed->GetGUID().GetCounter())") != std::string::npos &&
              guild.find("OverseerDecisions::GuildDeathAttackers(hits)") != std::string::npos &&
              guild.find("OverseerDecisions::GuildDeathTelemetry(hits)") != std::string::npos);

    std::string const record = Between(source, "void RecordDeath(Player* player)",
                                       "// ONE MOVEMENT OWNER PER FAMILY LEADER");
    Check("the row's mover falls back to the last hit when nothing was sampled",
          record.find("!attribution.sampled && hits.seen") != std::string::npos &&
              record.find("d.movement = OverseerDecisions::MoveGeneratorName(hits.movement);") !=
                  std::string::npos &&
              record.find("hits.movement") < record.find("OverseerDecisions::NameTheDriver(attribution)"));
    Check("the log line is only peeked: the death still takes the ring",
          record.find("TakeDeathHits(player->GetGUID().GetCounter())") != std::string::npos);
}

}  // namespace

int main()
{
    TheAccountCarriesTheTelemetry();
    TheWordsAreHonest();
    ThePetAndTheController();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("death telemetry: ok\n");
    return 0;
}
