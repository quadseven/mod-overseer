#include "overseer_decisions.h"
#include "overseer_movement_ownership.h"
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

// The column is the movement ownership module's in-memory adapter (#1312);
// the real module decides whether a claim or a release writes.
struct Book
{
    // BOOK_TYPES
    struct State { std::string target; };
    std::map<std::string, Refusal> _refused;
    std::map<std::string, LastWrite> _lastWrite;
    std::map<std::string, State> _state;
    std::map<std::string, ErrandEnd> _lastEnd;
    std::map<std::string, Landing> _landed;
    uint64_t _endSerial{0};
    unsigned persisted{0};
    OverseerMovement::InMemoryAimColumn column;
    OverseerMovement::MovementOwnership _wheel{column, 45};
    bool allowIntent{true};
    std::map<std::string, std::string> _claimRefusalSaid, _aimTooLongSaid;
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
                book.column.columns["Leader"].clear();
                Check(book.Claim("Leader", "vendor", owner), "fixture claim accepted", owner, active);
                book._state["Leader"] = {"vendor"};
            };
            claim();
            // The production cache can reuse an accepted claim without another
            // intent request; it must retain the previously recorded owner.
            book.allowIntent = false;
            TravelOwner held = TravelOwner::TownTrip;
            Check(book.Claim("Leader", "vendor", owner) &&
                      book.ClaimedBy("Leader", "vendor", held) && held == owner,
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
            // A claim without an owner cannot exist any more: the module
            // keeps the aim and its owner as one record (#1312), so the
            // legacy missing-claimant case is unrepresentable.
            claim();
            if (run || a.catching || a.home)
                continue;
            book.Refuse("Leader", "vendor", "it was killing this character");
            book.Release("Leader", "death-breaker replay");
            Check(book.column.columns["Leader"].empty() && !book.RunOwns("Leader", "vendor") &&
                  book._state.empty(),
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
            Check(book.column.columns["Leader"].empty(),
                  "refused reissue clears column again", owner, active);
        }
    }
    // AI-REVIEW 2026-11-01 [claude-haiku-5-5] EO-CLAIM-HEAD (PR #1306):
    // Run the full Claim, including its early refusals, before replaying death handling.
    for (unsigned refusal = 0; refusal < 4; ++refusal)
    {
        Adapter a;
        auto& book = a._travelAims;
        book.column.columns["Leader"] = refusal == 1 ? "banker" : "";
        auto const before = book.column.columns["Leader"];
        book.column.learnSkills["Leader"] = refusal == 2 ? 1 : 0;
        book.allowIntent = refusal != 3;
        auto const aim = refusal == 0 ? std::string(33, 'x') : "vendor";
        TravelOwner held = TravelOwner::TownTrip;
        Check(!book.Claim("Leader", aim, TravelOwner::TownTrip) &&
              !book.ClaimedBy("Leader", aim, held) && book.column.writes == 0 &&
              book.column.columns["Leader"] == before,
              "refused claim leaves owner and column untouched", TravelOwner::TownTrip, false);
    }
    if (!failures)
        std::cout << "PASS 16 owner/run combinations with guard, target, release and reissue controls\n";
    return failures ? 1 : 0;
}
