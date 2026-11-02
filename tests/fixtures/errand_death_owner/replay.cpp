#include "overseer_decisions.h"
#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace OverseerDecisions;
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)

// OWNER_KIND

// Model only the SQL transport. The actual ReleaseImpl chooses whether to write.
struct Database
{
    std::map<std::string, std::string> columns;
    void Execute(char const* format, std::string const& first, std::string const& second)
    {
        std::string const sql = format;
        if (sql == "UPDATE overseer_roster SET travel_npc = '{}' WHERE name = '{}'")
            columns[second] = first;
        else if (sql == "UPDATE overseer_roster SET travel_npc = '' WHERE name = '{}' "
                        "AND travel_npc = '{}'")
        {
            if (columns[first] == second)
                columns[first].clear();
        }
        else
            throw std::runtime_error("Unexpected replay SQL");
    }
} CharacterDatabase;

struct Book
{
    // BOOK_TYPES
    struct State { std::string target; };
    std::map<std::string, std::string> _claimed;
    std::map<std::string, TravelOwner> _claimOwner;
    std::map<std::string, Refusal> _refused;
    std::map<std::string, LastWrite> _lastWrite;
    std::map<std::string, State> _state;
    std::map<std::string, ErrandEnd> _lastEnd;
    std::map<std::string, Landing> _landed;
    std::map<std::string, time_t> _handback;
    uint64_t _endSerial{0};
    unsigned persisted{0}, learnSkill{0};
    bool allowIntent{true};
    std::map<std::string, std::string> _claimRefusalSaid, _aimTooLongSaid;
    std::string CurrentTravelNpc(std::string const& name) const
    {
        return CharacterDatabase.columns.at(name);
    }
    std::string Esc(std::string const& value) const { return value; }
    unsigned LearnSkillPending(std::string const&) const { return learnSkill; }
    LeaderIntentKind LeaderIntentNow(std::string const&) const { return LeaderIntentKind::None; }
    bool AskForLeader(std::string const&, LeaderIntentKind, std::string const&,
                      std::string const&, std::string const&) const { return allowIntent; }
    void EndLeaderColumnIntent(std::string const&, std::string const&) {}
    void KeepRefusal(std::string const&) { ++persisted; }
    // BOOK_METHODS
};

struct Adapter
{
    Book _travelAims;
    bool active{false}, catching{false}, home{false};
    bool AnyActiveRun() const { return active; }
    bool IsCatchingUp(std::string const&) const { return catching; }
    bool IsWalkingHome(std::string const&) const { return home; }
    ErrandDeathToll Assemble(std::string const& name, std::string const& target,
                            bool guarded)
    {
        // TOLL_ASSEMBLY
        return toll;
    }
};

unsigned failures = 0;
void Check(bool ok, char const* context, TravelOwner owner, bool active)
{
    if (!ok)
    {
        std::cerr << "FAIL " << context << " owner=" << static_cast<int>(owner)
                  << " active=" << active << '\n';
        ++failures;
    }
}

int main()
{
    for (auto owner : {TravelOwner::TownTrip, TravelOwner::Respec,
                       TravelOwner::TrainingStop, TravelOwner::Run,
                       TravelOwner::WalkBackIn, TravelOwner::Exit,
                       TravelOwner::CatchUp, TravelOwner::HomeErrand})
    {
        for (bool active : {false, true})
        {
            Adapter a;
            a.active = active;
            a.catching = owner == TravelOwner::CatchUp;
            a.home = owner == TravelOwner::HomeErrand;
            auto& book = a._travelAims;
            auto claim = [&] {
                CharacterDatabase.columns["Leader"].clear();
                Check(book.Claim("Leader", "vendor", owner), "fixture claim accepted", owner, active);
                book._state["Leader"] = {"vendor"};
            };
            claim();
            // The production cache can reuse an accepted claim without another
            // intent request; it must retain the previously recorded owner.
            book.allowIntent = false;
            Check(book.Claim("Leader", "vendor", owner) && book._claimOwner.at("Leader") == owner,
                  "cached accepted claim retains owner", owner, active);
            book.allowIntent = true;
            auto toll = a.Assemble("Leader", "vendor", false);
            toll.deaths = 3;
            bool const run = TravelOwnerIsALiveRun(owner);
            auto const wanted = active
                ? (run ? ErrandDeathRemedy::DeclineRunOwned
                       : (a.catching || a.home ? ErrandDeathRemedy::EndCatchUp
                                              : ErrandDeathRemedy::Release))
                : ErrandDeathRemedy::Release;
            Check(ErrandDeathBreaker(toll, {}).remedy == wanted,
                  "three-death remedy", owner, active);
            auto guarded = a.Assemble("Leader", "vendor", true);
            guarded.deaths = 3;
            Check(ErrandDeathBreaker(guarded, {}).remedy == ErrandDeathRemedy::Continue,
                  "guard remains", owner, active);
            auto different = a.Assemble("Leader", "banker", false);
            different.deaths = 3;
            Check(ErrandDeathBreaker(different, {}).remedy == ErrandDeathRemedy::Release,
                  "changed target is not owned", owner, active);
            book._claimOwner.erase("Leader");
            auto missing = a.Assemble("Leader", "vendor", false);
            missing.deaths = 3;
            // Legacy catch-up/home flags still end their walk; an unknown
            // ordinary claimant cannot invent a dungeon run.
            auto const missingWanted = active && (a.catching || a.home)
                ? ErrandDeathRemedy::EndCatchUp : ErrandDeathRemedy::Release;
            Check(ErrandDeathBreaker(missing, {}).remedy == missingWanted,
                  "missing claimant preserves only known walks", owner, active);
            claim();
            if (run || a.catching || a.home)
                continue;
            book.Refuse("Leader", "vendor", "it was killing this character");
            book.Release("Leader", "death-breaker replay");
            Check(CharacterDatabase.columns["Leader"].empty() && book._claimed.empty() &&
                  book._claimOwner.empty() && book._state.empty(),
                  "release clears claim and column", owner, active);
            Check(book.persisted == 1 && book.SecondsSinceRefused("Leader", "vendor") >= 0,
                  "release retains persisted refusal", owner, active);
            // Simulate another writer claiming the same aim on the next poll.
            claim();
            auto reissued = a.Assemble("Leader", "vendor", false);
            reissued.sinceRefused = book.SecondsSinceRefused("Leader", "vendor");
            reissued.strikes = 1;
            Check(ErrandDeathBreaker(reissued, {}).remedy == ErrandDeathRemedy::RefuseReissue,
                  "reissue is refused before any new death", owner, active);
            book.Release("Leader", "refused reissue");
            Check(CharacterDatabase.columns["Leader"].empty(),
                  "refused reissue clears column again", owner, active);
        }
    }
    // AI-REVIEW 2026-11-01 [claude-haiku-5-5] EO-CLAIM-HEAD (PR #1306):
    // Run the full Claim, including its early refusals, before replaying death handling.
    for (unsigned refusal = 0; refusal < 4; ++refusal)
    {
        Adapter a;
        auto& book = a._travelAims;
        CharacterDatabase.columns["Leader"] = refusal == 1 ? "banker" : "";
        auto const before = CharacterDatabase.columns["Leader"];
        book.learnSkill = refusal == 2 ? 1 : 0;
        book.allowIntent = refusal != 3;
        auto const aim = refusal == 0 ? std::string(33, 'x') : "vendor";
        Check(!book.Claim("Leader", aim, TravelOwner::TownTrip) && book._claimed.empty() &&
              book._claimOwner.empty() && CharacterDatabase.columns["Leader"] == before,
              "refused claim leaves owner and column untouched", TravelOwner::TownTrip, false);
    }
    if (!failures)
        std::cout << "PASS 16 owner/run combinations with guard, target, release and reissue controls\n";
    return failures ? 1 : 0;
}
