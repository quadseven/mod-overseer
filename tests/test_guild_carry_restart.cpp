/*
 * A carried guild seat after a restart (#1363).
 *
 * A carry adds `passive` to the member's non-combat and combat engines and
 * the run's end takes off only what the run added. The lease that records
 * that lived only in memory, so a member whose lists were saved mid-carry
 * logged back in after a restart with `passive` on both engines, the adopted
 * run read it as the member's own, and the run's end left it on for good.
 *
 * The row now carries the lease (`carry_seen`, `carry_pre`), the resume reads
 * it back, and GuildCarryLeaseOnAdopt says who owns each engine. The model
 * below follows ApplyGuildCarryTactics and ReturnGuildCarryLeases rule for
 * rule; tests/replay_guild_carry.py runs the real ones. The source contract at
 * the end holds the adapter to seeding the lease ahead of the first apply.
 */

#include "overseer_decisions.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>

namespace
{
using namespace OverseerDecisions;

char const* const ENGINES[] = {GUILD_CARRY_ENGINE_NON_COMBAT, GUILD_CARRY_ENGINE_COMBAT};

// The member's engines: which of them carry `passive`.
using Engines = std::set<std::string>;

struct Lease
{
    std::set<std::string> added, observed, preexisting;
};

// SeedGuildCarryLease.
void Seed(Lease& lease, GuildCarrySaved const& saved, Engines const& member)
{
    for (char const* engine : ENGINES)
        switch (GuildCarryLeaseOnAdopt(saved, engine, member.count(engine) != 0))
        {
            case GuildCarryAdoptEngine::Preexisting:
                lease.observed.insert(engine);
                lease.preexisting.insert(engine);
                break;
            case GuildCarryAdoptEngine::Added:
                lease.observed.insert(engine);
                lease.added.insert(engine);
                break;
            case GuildCarryAdoptEngine::Unobserved:
                break;
        }
}

// ApplyGuildCarryTactics.
void Apply(Lease& lease, Engines& member)
{
    for (char const* engine : ENGINES)
    {
        if (lease.observed.insert(engine).second && member.count(engine))
            lease.preexisting.insert(engine);
        if (!member.count(engine))
        {
            member.insert(engine);
            if (!lease.preexisting.count(engine))
                lease.added.insert(engine);
        }
    }
}

// ReturnGuildCarryLeases.
void Return(Lease& lease, Engines& member)
{
    for (std::string const& engine : lease.added)
        member.erase(engine);
    lease.added.clear();
}

// GuildRunJson's carry fields, then ReadGuildRunResume.
GuildCarrySaved RowRoundTrip(Lease const& lease)
{
    std::string seen, pre;
    for (char const* engine : ENGINES)
    {
        if (lease.observed.count(engine))
            seen += (seen.empty() ? "" : ",") + std::string(engine);
        if (lease.preexisting.count(engine))
            pre += (pre.empty() ? "" : ",") + std::string(engine);
    }
    std::string const row = "{\"phase\":\"inside\",\"map\":36,\"loot_items\":2,\"loot_notable\":[],"
                            "\"carry_seen\":\"" + seen + "\",\"carry_pre\":\"" + pre +
                            "\",\"members\":[{\"name\":\"First\",\"level_start\":20,\"deaths\":0}]}";
    GuildRunResume const resume = ReadGuildRunResume(row);
    assert(resume.inside && resume.mapId == 36 && resume.members.size() == 1);
    return resume.carry;
}

// A carried run from its start to its end. `savedMidCarry`: upstream saved
// the carried lists, so the login replays them. `restart`: the worldserver
// restarts while the run is inside and adopts it from its row.
Engines CarriedRun(Engines member, bool restart, bool savedMidCarry)
{
    Engines const before = member;
    Lease lease;
    Apply(lease, member);
    assert(member.size() == 2);
    if (restart)
    {
        GuildCarrySaved const saved = RowRoundTrip(lease);
        assert(saved.saved);
        // The login: the saved lists come back, or the engines reset to the
        // member's own strategies.
        member = savedMidCarry ? member : before;
        lease = Lease{};
        Seed(lease, saved, member);
        Apply(lease, member);
        assert(member.size() == 2);
    }
    Return(lease, member);
    return member;
}
}  // namespace

int main()
{
    // THE CASE (#1363): saved mid-carry, restarted, adopted, ended. Passive is
    // off both engines, as it is with no restart at all.
    assert(CarriedRun({}, true, true).empty());
    assert(CarriedRun({}, true, false).empty());
    assert(CarriedRun({}, false, false).empty());

    // A member's own `passive` stays, with or without a restart, on the engine
    // it was on before the carry began; the other engine is the carry's.
    Engines const own{GUILD_CARRY_ENGINE_COMBAT};
    assert(CarriedRun(own, false, false) == own);
    assert(CarriedRun(own, true, true) == own);
    assert(CarriedRun(own, true, false) == own);
    Engines const both{GUILD_CARRY_ENGINE_NON_COMBAT, GUILD_CARRY_ENGINE_COMBAT};
    assert(CarriedRun(both, false, false) == both);
    assert(CarriedRun(both, true, true) == both);

    // A row from before the lease rode it: no carry fields, so adoption reads
    // the member afresh, as it did before.
    GuildRunResume const old = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"map\":36,\"members\":[{\"name\":\"First\"}]}");
    assert(!old.carry.saved && old.carry.seen.empty() && old.carry.preexisting.empty());
    for (char const* engine : ENGINES)
    {
        assert(GuildCarryLeaseOnAdopt(old.carry, engine, true) == GuildCarryAdoptEngine::Unobserved);
        assert(GuildCarryLeaseOnAdopt(old.carry, engine, false) == GuildCarryAdoptEngine::Unobserved);
    }

    // The rule, engine by engine.
    GuildCarrySaved saved;
    saved.saved = true;
    saved.seen = {GUILD_CARRY_ENGINE_NON_COMBAT, GUILD_CARRY_ENGINE_COMBAT};
    saved.preexisting = {GUILD_CARRY_ENGINE_COMBAT};
    assert(GuildCarryLeaseOnAdopt(saved, GUILD_CARRY_ENGINE_NON_COMBAT, true) ==
           GuildCarryAdoptEngine::Added);
    assert(GuildCarryLeaseOnAdopt(saved, GUILD_CARRY_ENGINE_NON_COMBAT, false) ==
           GuildCarryAdoptEngine::Unobserved);
    assert(GuildCarryLeaseOnAdopt(saved, GUILD_CARRY_ENGINE_COMBAT, true) ==
           GuildCarryAdoptEngine::Preexisting);
    assert(GuildCarryLeaseOnAdopt(saved, GUILD_CARRY_ENGINE_COMBAT, false) ==
           GuildCarryAdoptEngine::Preexisting);
    // A lease that never looked at an engine (the member was not in the world
    // when the row was written) says nothing about it.
    GuildCarrySaved empty;
    empty.saved = true;
    assert(GuildCarryLeaseOnAdopt(empty, GUILD_CARRY_ENGINE_COMBAT, true) ==
           GuildCarryAdoptEngine::Unobserved);
    GuildRunResume const blank = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"carry_seen\":\"\",\"carry_pre\":\"\",\"members\":[]}");
    assert(blank.carry.saved && blank.carry.seen.empty() && blank.carry.preexisting.empty());
    // A member's object is never read for the run's fields.
    GuildRunResume const nested = ReadGuildRunResume(
        "{\"phase\":\"inside\",\"members\":[{\"name\":\"First\",\"carry_seen\":\"nc\"}]}");
    assert(!nested.carry.saved);

    // THE SOURCE CONTRACT: the row writes the lease, and the adoption seeds the
    // lease from the resume before the first ApplyGuildCarryTactics, as does
    // the drive for a member that was not in the world at adoption.
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::cerr << "cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    auto const body = [&text](char const* head, char const* tail)
    {
        std::size_t const start = text.find(head);
        if (start == std::string::npos)
        {
            std::cerr << "cannot find " << head << "\n";
            std::exit(EXIT_FAILURE);
        }
        std::size_t const end = text.find(tail, start);
        if (end == std::string::npos)
        {
            std::cerr << "cannot find the end of " << head << "\n";
            std::exit(EXIT_FAILURE);
        }
        return text.substr(start, end - start);
    };
    std::string const json = body("    std::string GuildRunJson(GuildRun const& run,",
                                  "    char const* DoGuildFinderRun(");
    assert(json.find("\\\"carry_seen\\\":") != std::string::npos);
    assert(json.find("\\\"carry_pre\\\":") != std::string::npos);
    assert(json.find("\\\"carry_seen\\\":") < json.find("\\\"members\\\":["));

    std::string const adopt = body("    void DriveGuildRunAdoptions(std::time_t now)",
                                   "    void DriveGuildRunStrays(std::time_t now)");
    std::size_t const seeded = adopt.find("run.carrySaved = adoption.resume.carry;");
    std::size_t const seed = adopt.find("SeedGuildCarryLease(run.carried, run.id, run.carrySaved)");
    std::size_t const written = adopt.find("GuildRunJson(run, \"inside\"");
    std::size_t const applied = adopt.find("ApplyGuildCarryTactics(run);");
    assert(seeded != std::string::npos && seed != std::string::npos);
    assert(written != std::string::npos && applied != std::string::npos);
    assert(seeded < seed && seed < written && seed < applied);

    std::string const drive = body("    bool DriveGuildRun(GuildRun& run, std::time_t now)",
                                   "ApplyGuildCarryTactics(run);");
    assert(drive.find("SeedGuildCarryLease(run.carried, run.id, run.carrySaved)") !=
           std::string::npos);
    return EXIT_SUCCESS;
}
