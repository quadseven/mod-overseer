/*
 * Every dungeon death says what killed it.
 *
 * The live failure this pins. The Deadmines guild runs wiped 45 times in 62
 * runs (2026-10-08 to 2026-10-10) and not one death row says why. Run 507's
 * nine deaths on 2026-10-10 (11:10 to 11:40 America/New_York) were written
 * with health_at_death 0, max_health_at_death 0, in_combat -1 and damage_1..3
 * NULL. On 2026-10-09, 0 of 254 deaths in a dungeon and 0 of 8,087 deaths in
 * all carried a sample: the row's context came only from caches the 5 s
 * snapshot fills for the five roster characters, and the guild members the
 * table has also covered since 2026-10-05 were never sampled. A death hook
 * cannot read it back either: the core zeroes the health, stops the combat
 * and empties the attacker list before any death hook runs, which is why the
 * guild-death log line says "with 0 attacker(s) on it" for every death.
 *
 * So the damage hook keeps the last hits each player in a dungeon took, with
 * the state each hit found, and the death row reads them back: the last
 * damage and its sources, the health and mana before the killing blow, who
 * had the member as its target, and how many enemies were in combat with it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp and the migration to pin the wiring (run from the repo
 * root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::AccountForDeathHits;
using OverseerDecisions::DEATH_DAMAGE_HISTORY_SIZE;
using OverseerDecisions::DEATH_HIT_RING_SIZE;
using OverseerDecisions::DEATH_HIT_WINDOW_MS;
using OverseerDecisions::DEATH_TARGETED_BY_MAX;
using OverseerDecisions::DeathHit;
using OverseerDecisions::DeathHitAccount;
using OverseerDecisions::DeathHitRing;
using OverseerDecisions::DeathSampleLine;
using OverseerDecisions::NameTheAttackers;
using OverseerDecisions::RememberDeathHit;

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

DeathHit Hit(std::int64_t atMs, std::uint32_t amount, char const* source, std::uint32_t health,
             std::uint32_t mana, std::uint32_t engaged, char const* targetedBy = "")
{
    DeathHit hit;
    hit.atMs = atMs;
    hit.amount = amount;
    hit.source = source;
    hit.healthBefore = health;
    hit.maxHealth = 673;
    hit.manaBefore = mana;
    hit.maxMana = 0;
    hit.inCombat = true;
    hit.enemiesEngaged = engaged;
    hit.targetedBy = targetedBy;
    return hit;
}

// Durg, run 507's tank (673 health, a warrior), at 11:11:39 to 11:11:48.
DeathHitRing DurgInTheLumberMill()
{
    DeathHitRing ring;
    RememberDeathHit(ring, Hit(100000, 30, "Defias Miner", 400, 0, 2));
    RememberDeathHit(ring, Hit(104000, 44, "Goblin Woodcarver", 370, 0, 3));
    RememberDeathHit(ring, Hit(106000, 41, "Goblin Woodcarver", 326, 0, 3));
    RememberDeathHit(ring, Hit(107500, 12, "Defias Miner", 285, 0, 3));
    RememberDeathHit(ring, Hit(108000, 38, "Goblin Woodcarver", 36, 0, 3,
                               "Goblin Woodcarver x2, Defias Miner"));
    return ring;
}

void TheRingKeepsTheNewestHits()
{
    DeathHitRing ring;
    for (std::int64_t i = 0; i < static_cast<std::int64_t>(DEATH_HIT_RING_SIZE) + 4; ++i)
        RememberDeathHit(ring, Hit(i * 1000, static_cast<std::uint32_t>(i), "Defias Miner", 100, 0, 1));
    Check("the ring is bounded", ring.size() == DEATH_HIT_RING_SIZE);
    Check("oldest first, newest last",
          ring.front().amount == 4 && ring.back().amount == DEATH_HIT_RING_SIZE + 3);
}

void ADeathIsReadFromTheHitsBeforeIt()
{
    DeathHitAccount const a = AccountForDeathHits(DurgInTheLumberMill(), 108400);
    Check("the hits are seen", a.seen);
    Check("the health before the killing blow, not 0", a.healthBefore == 36 && a.maxHealth == 673);
    Check("a warrior has no mana bar", a.manaBefore == 0 && a.maxMana == 0);
    Check("in combat, three enemies on it", a.inCombat && a.enemiesEngaged == 3);
    Check("who had it as their target", a.targetedBy == "Goblin Woodcarver x2, Defias Miner");
    Check("the last three hits, oldest first, with their sources",
          a.damage.size() == DEATH_DAMAGE_HISTORY_SIZE && a.damage[0].amount == 41 &&
              a.damage[0].source == "Goblin Woodcarver" && a.damage[1].source == "Defias Miner" &&
              a.damage[2].amount == 38);
    Check("seconds before the death", a.damage[0].secondsBeforeDeath == 2 &&
                                          a.damage[2].secondsBeforeDeath == 0);
}

void AHealersManaIsRead()
{
    DeathHitRing ring;
    DeathHit hit = Hit(50000, 120, "Goblin Engineer", 90, 31, 2, "Goblin Engineer");
    hit.maxHealth = 487;
    hit.maxMana = 620;
    RememberDeathHit(ring, hit);
    DeathHitAccount const a = AccountForDeathHits(ring, 50500);
    Check("Dunga's mana before the killing blow", a.manaBefore == 31 && a.maxMana == 620);
}

void OldHitsBelongToNoDeath()
{
    DeathHitRing ring = DurgInTheLumberMill();
    Check("hits older than the window are not this death's",
          !AccountForDeathHits(ring, 108000 + DEATH_HIT_WINDOW_MS + 1).seen);
    Check("an empty ring is unsampled, not zero", !AccountForDeathHits(DeathHitRing{}, 1).seen);
    DeathHitRing late;
    RememberDeathHit(late, Hit(200000, 10, "Defias Miner", 50, 0, 1));
    Check("a hit after the death is not before it", !AccountForDeathHits(late, 199000).seen);
}

void TheTargetedByIsReadFromTheKillingBlowOrTheNewestThatHasIt()
{
    DeathHitRing ring = DurgInTheLumberMill();
    ring.back().targetedBy.clear();
    ring[ring.size() - 2].targetedBy = "Defias Miner";
    Check("the newest hit that read it",
          AccountForDeathHits(ring, 108400).targetedBy == "Defias Miner");
}

void AttackersAreCountedAndCapped()
{
    Check("most first, then by name",
          NameTheAttackers({"Defias Miner", "Goblin Woodcarver", "Goblin Woodcarver"}) ==
              "Goblin Woodcarver x2, Defias Miner");
    Check("ties by name", NameTheAttackers({"Defias Miner", "Defias Evoker"}) ==
                              "Defias Evoker, Defias Miner");
    Check("nobody", NameTheAttackers({}).empty());
    std::vector<std::string> many;
    for (int i = 0; i < 40; ++i)
        many.push_back("A Very Long Creature Name " + std::to_string(i));
    Check("never longer than the column", NameTheAttackers(many).size() <= DEATH_TARGETED_BY_MAX);
}

void TheProofLineSaysItAll()
{
    std::string const line = DeathSampleLine("Durg", 36, AccountForDeathHits(DurgInTheLumberMill(), 108400));
    Check("the proof line names the state before the killing blow",
          line == "death sample - 'Durg' in map 36 had 36 of 673 health and no mana bar before the "
                  "killing blow, in combat, 3 enemies engaged, targeted by Goblin Woodcarver x2, "
                  "Defias Miner; last hits: Goblin Woodcarver 38 (0s before), Defias Miner 12 (0s "
                  "before), Goblin Woodcarver 41 (2s before)");
    Check("an unsampled death says so",
          DeathSampleLine("Durg", 36, DeathHitAccount{}).find("unsampled") != std::string::npos);
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
    std::string const meter = Between(source, "class OverseerMeterScript : public UnitScript",
                                      "class OverseerAuctionScript");
    Check("every hit a player takes reaches the dungeon hit ring",
          meter.find("NoteDungeonHit(attacker, hurt, damage);") != std::string::npos);

    std::string const note = Between(source, "static void NoteDungeonHit(", "static void SweepDeathHits(");
    Check("only inside a dungeon", note.find("if (!map || !map->IsDungeon())") != std::string::npos);
    Check("the health, the mana and the combat before the hit",
          note.find("hit.healthBefore = hurt->GetHealth();") != std::string::npos &&
              note.find("hurt->GetPower(POWER_MANA)") != std::string::npos &&
              note.find("hit.inCombat = hurt->IsInCombat();") != std::string::npos);
    Check("the enemies in combat with it",
          note.find("GetCombatManager().GetPvECombatRefs().size()") != std::string::npos);
    Check("who targets it, read on the killing blow",
          note.find("if (damage >= hit.healthBefore)") != std::string::npos &&
              note.find("OverseerDecisions::NameTheAttackers(names)") != std::string::npos);

    std::string const record = Between(source, "void RecordDeath(Player* player)",
                                       "// ONE MOVEMENT OWNER PER FAMILY LEADER");
    Check("the death takes its hits",
          record.find("TakeDeathHits(player->GetGUID().GetCounter())") != std::string::npos);
    Check("and the row is filled from them",
          record.find("d.healthAtDeath = hits.healthBefore;") != std::string::npos &&
              record.find("d.manaAtDeath = hits.manaBefore;") != std::string::npos &&
              record.find("d.targetedBy = hits.targetedBy;") != std::string::npos &&
              record.find("d.damage = hits.damage;") != std::string::npos &&
              record.find("d.inCombat = hits.inCombat ? 1 : 0;") != std::string::npos);
    Check("the hits are read after the gap rule decides, and before the queue",
          record.find("TakeDeathHits(") < record.find("if (!record)") &&
              record.find("d.damage = hits.damage;") < record.find("g_deathQueue.push_back"));
    Check("a dungeon death says its sample in the log",
          record.find("OverseerDecisions::DeathSampleLine(d.characterName, d.mapId, said)") !=
              std::string::npos);

    std::string const write = Between(source, "        bool const hitColumns = DeathHitColumnsPresent();",
                                      "        CharacterDatabase.Execute(ss.str().c_str());");
    Check("the writer asks whether the columns exist", !write.empty());
    Check("the four columns are written, NULL when unsampled",
          write.find("mana_at_death, max_mana_at_death, enemies_engaged, targeted_by") !=
                  std::string::npos &&
              write.find(",NULL,NULL,NULL,NULL") != std::string::npos);
    Check("the ring is swept on the death timer",
          source.find("FlushLevels();\n            SweepDeathHits();") != std::string::npos);

    std::string const migration = Read("data/sql/characters/base/2026_10_10_01_overseer_death_hits.sql");
    Check("the migration adds the four columns, guarded",
          migration.find("ADD COLUMN `mana_at_death`") != std::string::npos &&
              migration.find("ADD COLUMN `max_mana_at_death`") != std::string::npos &&
              migration.find("ADD COLUMN `enemies_engaged`") != std::string::npos &&
              migration.find("ADD COLUMN `targeted_by` VARCHAR(255)") != std::string::npos &&
              migration.find("INFORMATION_SCHEMA.COLUMNS") != std::string::npos);
}

}  // namespace

int main()
{
    TheRingKeepsTheNewestHits();
    ADeathIsReadFromTheHitsBeforeIt();
    AHealersManaIsRead();
    OldHitsBelongToNoDeath();
    TheTargetedByIsReadFromTheKillingBlowOrTheNewestThatHasIt();
    AttackersAreCountedAndCapped();
    TheProofLineSaysItAll();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("dungeon death sample: all checks passed\n");
    return 0;
}
