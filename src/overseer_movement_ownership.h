#ifndef OVERSEER_MOVEMENT_OWNERSHIP_H
#define OVERSEER_MOVEMENT_OWNERSHIP_H

// MOVEMENT OWNERSHIP: WHO HOLDS A CHARACTER'S WHEEL (#1312).
//
// `overseer_roster.travel_npc` is the one roster column with two writers: this
// module's drives and the website's bridge (economy, positional and profession
// errands). Taking it, giving it back and asking who holds it is one concept,
// and its rules used to live at the call sites: the claim fences, the exact-aim
// release a dungeon run makes when it ends while a newer aim stands (#812,
// #813, #814), the hand-back clock that keeps the quest drive off the wheel,
// and a raw compare-and-swap in the run finalizer that sat outside the book.
//
// This header is that concept behind one interface. It has no AzerothCore
// include, so the decisions runner compiles it and the tests drive it through
// InMemoryAimColumn. The production adapter, the roster table, lives in
// mod_overseer.cpp beside TravelAimBook, which keeps the errand memory
// (`_state`, landings, refusals) and asks this for every column decision.
//
// World thread only, like every per-character map in the module. Lost on a
// restart, which forgets the claims; the fences then treat every standing aim
// as foreign, which is the safe reading.

#include "overseer_decisions.h"

#include <cstdint>
#include <ctime>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace OverseerMovement
{

// THE SEAM. Every read and write of the travel column goes through one of
// these five calls, and nothing else in the module names the column in SQL.
class AimColumn
{
public:
    virtual ~AimColumn() = default;
    // `travel_npc` as it stands now; empty for no aim or no row.
    virtual std::string Read(std::string const& name) = 0;
    // `learn_skill` as it stands now; zero means no profession errand.
    virtual uint32_t PendingLearnSkill(std::string const& name) = 0;
    // Take the column: write `aim` whatever stands there.
    virtual void Write(std::string const& name, std::string const& aim) = 0;
    // Give it back: blank the column only while it still holds `expected`.
    // Queued in production, as every ordinary release always was.
    virtual void ClearIfStill(std::string const& name, std::string const& expected) = 0;
    // The same compare-and-swap, synchronous and for an enabled row only, so
    // the read that follows sees the blank. For a leftover, a poisoned aim and
    // a finished run's exact leg.
    virtual void ClearNowIfStill(std::string const& name, std::string const& expected) = 0;
};

// THE TEST ADAPTER. One row per name, enabled unless disabled. Counts writes
// and reads, so a test can say "nothing was written", not merely "the value is
// the same".
class InMemoryAimColumn final : public AimColumn
{
public:
    std::map<std::string, std::string> columns;
    std::map<std::string, uint32_t> learnSkills;
    std::set<std::string> disabled;
    unsigned writes{0};
    unsigned reads{0};

    std::string Read(std::string const& name) override
    {
        ++reads;
        auto const it = columns.find(name);
        return it == columns.end() ? std::string() : it->second;
    }
    uint32_t PendingLearnSkill(std::string const& name) override
    {
        auto const it = learnSkills.find(name);
        return it == learnSkills.end() ? 0 : it->second;
    }
    void Write(std::string const& name, std::string const& aim) override
    {
        ++writes;
        columns[name] = aim;
    }
    void ClearIfStill(std::string const& name, std::string const& expected) override
    {
        ++writes;
        auto const it = columns.find(name);
        if (it != columns.end() && it->second == expected)
            it->second.clear();
    }
    void ClearNowIfStill(std::string const& name, std::string const& expected) override
    {
        ++writes;
        auto const it = columns.find(name);
        if (it != columns.end() && it->second == expected && !disabled.count(name))
            it->second.clear();
    }
};

enum class ClaimAnswer : uint8_t
{
    AlreadyDriving,  // the walk in flight is this aim and the column still says so
    Written,         // the aim is now in the column, written by this call
    TooLong,         // the aim does not fit the column; nothing was read or written
    Fenced,          // a profession or foreign errand stands; see `verdict`
    LeaderBusy,      // a family leader held by another intent; nothing written
};

struct LeaderIntentSnapshot
{
    OverseerDecisions::LeaderIntentKind kind{OverseerDecisions::LeaderIntentKind::None};
    std::string owner;
    std::string target;
    time_t since{0};
    std::uint32_t changes{0};
};

struct ClaimResult
{
    ClaimAnswer answer{ClaimAnswer::TooLong};
    // ReadTravelClaim's verdict, meaningful once the fences were read
    // (Written, Fenced, LeaderBusy).
    OverseerDecisions::TravelClaim verdict{OverseerDecisions::TravelClaim::Write};
    std::string column;    // the column as read, before any write
    uint32_t learnSkill{0};
    struct LeaderAsk
    {
        bool asked{false};
        OverseerDecisions::LeaderIntentKind kind{OverseerDecisions::LeaderIntentKind::None};
        std::string owner;
        std::string target;
        std::string why;
        LeaderIntentSnapshot before;
        OverseerDecisions::LeaderIntentVerdict verdict;
    } leaderAsk;
    // True when the aim is in the column after the call.
    bool Holds() const
    {
        return answer == ClaimAnswer::AlreadyDriving || answer == ClaimAnswer::Written;
    }
};

struct ReleaseResult
{
    // An exact-aim release found a newer aim, or a newer owner's claim, and
    // did nothing: no write, no claim forgotten, no clock stamped. The newer
    // owner will release it.
    bool leftNewerAim{false};
    std::string standing;  // the column as read
    bool anyClaim{false};  // a claim was held when the release began
    // The aim that ended: the claimed aim when there was a claim, else the
    // standing column.
    std::string endedAim;
    // Non-empty when the gate kept the column write from erasing an aim this
    // module did not claim (TravelReleaseGate's clause).
    std::string fence;
};

enum class RunAimRetire : uint8_t
{
    NotTried,      // not the run's to clear, or not its exact positional leg
    Retired,       // the column is empty after the compare-and-swap
    NewerAimLeft,  // something newer stood, and still stands
};

struct RunAimRetireResult
{
    RunAimRetire answer{RunAimRetire::NotTried};
    std::string after;  // the column read back after the attempt
};

struct LeaderColumnEnd
{
    bool ended{false};
    OverseerDecisions::LeaderIntentKind kind{OverseerDecisions::LeaderIntentKind::None};
    std::string owner;
    OverseerDecisions::LeaderIntentEnd how{OverseerDecisions::LeaderIntentEnd::Abandoned};
    time_t since{0};
};

struct LeaderColumnEndDecision
{
    OverseerDecisions::LeaderIntentKind kind{OverseerDecisions::LeaderIntentKind::None};
    OverseerDecisions::LeaderIntentEnd how{OverseerDecisions::LeaderIntentEnd::Abandoned};
};

inline LeaderColumnEndDecision DecideLeaderColumnEnd(
    OverseerDecisions::LeaderIntentKind kind, std::string const& why)
{
    switch (kind)
    {
        case OverseerDecisions::LeaderIntentKind::EconomyErrand:
        case OverseerDecisions::LeaderIntentKind::TownErrand:
        case OverseerDecisions::LeaderIntentKind::Respec:
        case OverseerDecisions::LeaderIntentKind::TrainingStop:
        case OverseerDecisions::LeaderIntentKind::Hearth:
        case OverseerDecisions::LeaderIntentKind::DungeonRun:
            break;
        default:
            return {};
    }

    bool const done = why.find("arrived") != std::string::npos ||
                      why.find("reached") != std::string::npos ||
                      why.find("done") != std::string::npos ||
                      why.find("stepped through") != std::string::npos;
    return {kind, done ? OverseerDecisions::LeaderIntentEnd::Completed
                       : OverseerDecisions::LeaderIntentEnd::Failed};
}

class MovementOwnership
{
public:
    struct TravelOwnerIntent
    {
        OverseerDecisions::LeaderIntentKind kind{OverseerDecisions::LeaderIntentKind::None};
        std::string owner;
    };

    static TravelOwnerIntent ForTravelOwner(OverseerDecisions::TravelOwner owner)
    {
        return IntentForTravelOwner(owner);
    }

    MovementOwnership(AimColumn& column, time_t handbackSeconds)
        : _column(column), _handbackSeconds(handbackSeconds)
    {
    }

    // The leader book is part of the same movement owner as the travel column.
    // The world script supplies its roster and live configuration; this module
    // owns the dwell, arbitration state, preferences, and end rules.
    void ConfigureLeaderIntents(bool enabled, OverseerDecisions::LeaderIntentLimits limits)
    {
        _leaderIntentsEnabled = enabled;
        _leaderLimits = limits;
    }

    bool LeaderIntentsEnabled() const { return _leaderIntentsEnabled; }

    void SetLeaders(std::set<std::string> leaders)
    {
        for (auto it = _leaderStates.begin(); it != _leaderStates.end();)
            it = leaders.count(it->first) ? std::next(it) : _leaderStates.erase(it);
        for (auto it = _leaderPreferences.begin(); it != _leaderPreferences.end();)
            it = leaders.count(it->first) ? std::next(it) : _leaderPreferences.erase(it);
        _leaders.swap(leaders);
    }

    bool LeaderIntentApplies(std::string const& name) const
    {
        return _leaderIntentsEnabled && _leaders.count(name) != 0;
    }

    void ClearLeaderPreferences() { _leaderPreferences.clear(); }

    void SetLeaderPreference(std::string const& name,
                             OverseerDecisions::LeaderIntentPreference preference)
    {
        if (_leaders.count(name))
            _leaderPreferences[name] = preference;
    }

    OverseerDecisions::LeaderIntentPreference LeaderPreferenceFor(std::string const& name) const
    {
        auto const it = _leaderPreferences.find(name);
        return it == _leaderPreferences.end() ? OverseerDecisions::LeaderIntentPreference{}
                                              : it->second;
    }

    LeaderIntentSnapshot LeaderIntentStateFor(std::string const& name) const
    {
        auto const it = _leaderStates.find(name);
        if (it == _leaderStates.end())
            return {};
        OverseerDecisions::LeaderIntentState const& state = it->second;
        return {state.kind, state.owner, state.target, state.since, state.changes};
    }

    OverseerDecisions::LeaderIntentVerdict AskForLeader(
        std::string const& name, OverseerDecisions::LeaderIntentKind kind,
        std::string const& owner, std::string const& target, std::string const& why, time_t now)
    {
        OverseerDecisions::LeaderIntentVerdict verdict;
        if (!LeaderIntentApplies(name))
        {
            verdict.answer = OverseerDecisions::LeaderIntentAnswer::Granted;
            return verdict;
        }
        OverseerDecisions::LeaderIntentRequest request;
        request.kind = kind;
        request.owner = owner;
        request.target = target;
        request.why = why;
        return OverseerDecisions::AskLeaderIntent(_leaderStates[name], request, now, _leaderLimits,
                                                   LeaderPreferenceFor(name));
    }

    bool EndForLeader(std::string const& name, OverseerDecisions::LeaderIntentKind kind,
                      std::string const& owner, OverseerDecisions::LeaderIntentEnd how, time_t now)
    {
        if (!LeaderIntentApplies(name))
            return false;
        auto const it = _leaderStates.find(name);
        return it != _leaderStates.end() &&
               OverseerDecisions::EndLeaderIntent(it->second, kind, owner, how, now, _leaderLimits);
    }

    OverseerDecisions::LeaderIntentKind CurrentLeaderIntent(std::string const& name,
                                                            time_t now) const
    {
        if (!LeaderIntentApplies(name))
            return OverseerDecisions::LeaderIntentKind::None;
        auto const it = _leaderStates.find(name);
        if (it == _leaderStates.end() ||
            OverseerDecisions::LeaderIntentFree(it->second, now, _leaderLimits))
            return OverseerDecisions::LeaderIntentKind::None;
        return it->second.kind;
    }

    void WaiveLeaderDwell(std::string const& name)
    {
        if (!LeaderIntentApplies(name))
            return;
        auto const it = _leaderStates.find(name);
        if (it != _leaderStates.end())
            OverseerDecisions::WaiveLeaderIntentDwell(it->second);
    }

    bool LeaderWalksUnderNewRpg(std::string const& name, time_t now) const
    {
        return OverseerDecisions::LeaderIntentWalksUnderNewRpg(CurrentLeaderIntent(name, now));
    }

    std::vector<OverseerDecisions::LeaderIntentRequest> LeaderIntentsOnTheTable(
        std::string const& name, time_t now) const
    {
        if (!LeaderIntentApplies(name))
            return {};
        auto const it = _leaderStates.find(name);
        if (it == _leaderStates.end())
            return {};
        return OverseerDecisions::LeaderIntentsOnTheTable(it->second, now, _leaderLimits,
                                                           LeaderPreferenceFor(name));
    }

    bool NoteLeaderStayLeak(std::string const& name)
    {
        return _leaders.count(name) && _stayLeakSaid.insert(name).second;
    }

    void ForgetLeaderStayLeak(std::string const& name) { _stayLeakSaid.erase(name); }

    LeaderColumnEnd EndColumnLeaderIntent(std::string const& name, std::string const& why,
                                          time_t now)
    {
        LeaderColumnEnd result;
        result.kind = CurrentLeaderIntent(name, now);
        LeaderColumnEndDecision const decision = DecideLeaderColumnEnd(result.kind, why);
        if (decision.kind == OverseerDecisions::LeaderIntentKind::None)
        {
            result.kind = OverseerDecisions::LeaderIntentKind::None;
            return result;
        }
        LeaderIntentSnapshot const state = LeaderIntentStateFor(name);
        result.kind = decision.kind;
        result.owner = state.owner;
        result.since = state.since;
        result.how = decision.how;
        result.ended = EndForLeader(name, result.kind, result.owner, result.how, now);
        return result;
    }

    // TAKE THE WHEEL for `owner`. `cachedDrive` is the caller's word that its
    // walk in flight is already this aim; it is trusted only while the column
    // still holds it, because another owner can change the column under a
    // cached drive. `leaderTurn`, asked only once every fence passed, answers
    // whether a family leader's intent book lets this owner act now; empty
    // means no leader rule applies.
    ClaimResult Claim(std::string const& name, std::string const& target,
                      OverseerDecisions::TravelOwner owner, bool cachedDrive, time_t now,
                      bool errandLetGo = false)
    {
        ClaimResult result;
        if (!OverseerDecisions::TravelAimFitsColumn(target))
        {
            result.answer = ClaimAnswer::TooLong;
            return result;
        }
        result.column = _column.Read(name);
        if (cachedDrive && result.column == target)
        {
            result.answer = ClaimAnswer::AlreadyDriving;
            return result;
        }
        OverseerDecisions::TravelClaimFacts facts = FactsFor(name, target, owner, result.column, errandLetGo);
        result.learnSkill = facts.learnSkill;
        result.verdict = OverseerDecisions::ReadTravelClaim(facts);
        if (!Writes(result.verdict))
        {
            result.answer = ClaimAnswer::Fenced;
            return result;
        }
        if (LeaderIntentApplies(name))
        {
            TravelOwnerIntent const intent = IntentForTravelOwner(owner);
            bool const fetchAlreadyHoldsLeader =
                intent.kind == OverseerDecisions::LeaderIntentKind::FetchMember &&
                CurrentLeaderIntent(name, now) == OverseerDecisions::LeaderIntentKind::FetchMember;
            if (!fetchAlreadyHoldsLeader)
            {
                result.leaderAsk.asked = true;
                result.leaderAsk.kind = intent.kind;
                result.leaderAsk.owner = intent.owner;
                result.leaderAsk.target = target;
                result.leaderAsk.why = "claims the travel column";
                result.leaderAsk.before = LeaderIntentStateFor(name);
                result.leaderAsk.verdict = AskForLeader(
                    name, intent.kind, intent.owner, target, result.leaderAsk.why, now);
                if (result.leaderAsk.verdict.answer ==
                    OverseerDecisions::LeaderIntentAnswer::Deferred)
                {
                    result.answer = ClaimAnswer::LeaderBusy;
                    return result;
                }
            }
        }
        _column.Write(name, target);
        _claims[name] = Held{target, owner};
        result.answer = ClaimAnswer::Written;
        return result;
    }

    // Would Claim refuse on its width or its fences? Reads, never writes. The
    // leader's turn is not forecast: it is a question of whose turn, not whose
    // errand.
    bool FenceRefuses(std::string const& name, std::string const& target,
                      OverseerDecisions::TravelOwner owner, bool cachedDrive,
                      bool errandLetGo = false)
    {
        if (!OverseerDecisions::TravelAimFitsColumn(target))
            return true;
        std::string const column = _column.Read(name);
        if (cachedDrive && column == target)
            return false;
        return !Writes(OverseerDecisions::ReadTravelClaim(FactsFor(name, target, owner, column, errandLetGo)));
    }

    // GIVE THE WHEEL BACK. Every ordinary release, in any drive. The column is
    // blanked only by compare-and-swap on what was read, and only when the
    // gate says the aim is this module's to erase; the claim is forgotten and
    // the hand-back clock stamped either way.
    ReleaseResult Release(std::string const& name, time_t now, bool inertFollowerAim = false)
    {
        return ReleaseImpl(name, nullptr, now, inertFollowerAim);
    }

    // A DUNGEON RUN ENDING. Ends only the run's own claim on exactly
    // `expectedAim`: a newer aim, or a newer owner's claim on the same aim,
    // is left with its owner. `mayClearRecordedAim` says whether the run's
    // recorded leg is still its to retire (see RetireRunAim): true when no
    // claim is remembered (a restart lost it) or the remembered claim is the
    // run's own on that leg.
    ReleaseResult ReleaseRunAim(std::string const& name, std::string const& expectedAim,
                                time_t now, bool& mayClearRecordedAim)
    {
        mayClearRecordedAim = true;
        auto const held = _claims.find(name);
        if (held != _claims.end())
            mayClearRecordedAim = OverseerDecisions::TravelOwnerIsALiveRun(held->second.owner) &&
                                  held->second.aim == expectedAim;
        return ReleaseImpl(name, &expectedAim, now);
    }

    // THE RUN'S EXACT FINAL LEG. Release protects every foreign `at:` aim, so
    // a run whose claim was lost to a restart cannot clear its own last leg
    // that way. The run keeps an exact record of it, so this retires only that
    // positional value, synchronously, and reads back what stands.
    RunAimRetireResult RetireRunAim(std::string const& name, std::string const& recordedRunAim,
                                    bool mayClearRecordedAim)
    {
        RunAimRetireResult result;
        if (!mayClearRecordedAim ||
            !OverseerDecisions::RunAimMayBeReleased(_column.Read(name), recordedRunAim))
            return result;
        _column.ClearNowIfStill(name, recordedRunAim);
        result.after = _column.Read(name);
        result.answer = result.after.empty() ? RunAimRetire::Retired : RunAimRetire::NewerAimLeft;
        return result;
    }

    // THE COLUMN WAS EMPTIED OUTSIDE THIS MODULE (the bridge re-aimed the
    // family). Nothing to write; the claim goes and the hand-back clock is
    // stamped, because the character may be mid-walk under an aim nobody
    // renews.
    void ReleaseVanished(std::string const& name, time_t now)
    {
        _claims.erase(name);
        _handback[name] = now;
    }

    // A POINT AIM LEFT FROM BEFORE THIS PROCESS STARTED (#658): cleared by
    // compare-and-swap on the aim itself, and any claim forgotten.
    void ForgetLeftover(std::string const& name, std::string const& target)
    {
        _column.ClearNowIfStill(name, target);
        _claims.erase(name);
    }

    // A POISONED AIM (#824): the same compare-and-swap, after the release that
    // already ended the claim.
    void ClearPoisonedAim(std::string const& name, std::string const& target)
    {
        _column.ClearNowIfStill(name, target);
    }

    // Did this module write the aim now claimed for `name`, as `target`?
    bool HoldsClaim(std::string const& name, std::string const& target) const
    {
        auto const it = _claims.find(name);
        return it != _claims.end() && it->second.aim == target;
    }

    // Which owner claimed `target` for `name`; false when this module did not.
    bool ClaimedBy(std::string const& name, std::string const& target,
                   OverseerDecisions::TravelOwner& owner) const
    {
        auto const it = _claims.find(name);
        if (it == _claims.end() || it->second.aim != target)
            return false;
        owner = it->second.owner;
        return true;
    }

    // HAS THE WHEEL BEEN HANDED BACK TOO RECENTLY FOR THE QUEST DRIVE? Only a
    // release stamps the clock; a claim never does. A lapsed entry is swept.
    bool WithinHandbackGrace(std::string const& name, time_t now)
    {
        auto const it = _handback.find(name);
        if (it == _handback.end())
            return false;
        if (now - it->second < _handbackSeconds)
            return true;
        _handback.erase(it);
        return false;
    }

private:
    struct Held
    {
        std::string aim;
        OverseerDecisions::TravelOwner owner{};
    };

    static bool Writes(OverseerDecisions::TravelClaim verdict)
    {
        return verdict == OverseerDecisions::TravelClaim::Write ||
               verdict == OverseerDecisions::TravelClaim::Outrank ||
               verdict == OverseerDecisions::TravelClaim::Preempt;
    }

    static TravelOwnerIntent IntentForTravelOwner(OverseerDecisions::TravelOwner owner)
    {
        TravelOwnerIntent intent;
        switch (owner)
        {
            case OverseerDecisions::TravelOwner::HomeErrand:
                intent.kind = OverseerDecisions::LeaderIntentKind::Hearth;
                intent.owner = "home errand";
                return intent;
            case OverseerDecisions::TravelOwner::CatchUp:
                intent.kind = OverseerDecisions::LeaderIntentKind::FetchMember;
                intent.owner = "fetch";
                return intent;
            case OverseerDecisions::TravelOwner::Run:
            case OverseerDecisions::TravelOwner::WalkBackIn:
            case OverseerDecisions::TravelOwner::Exit:
                intent.kind = OverseerDecisions::LeaderIntentKind::DungeonRun;
                intent.owner = "dungeon run";
                return intent;
            case OverseerDecisions::TravelOwner::Respec:
                intent.kind = OverseerDecisions::LeaderIntentKind::Respec;
                intent.owner = "respec";
                return intent;
            case OverseerDecisions::TravelOwner::TrainingStop:
                intent.kind = OverseerDecisions::LeaderIntentKind::TrainingStop;
                intent.owner = "training stop";
                return intent;
            case OverseerDecisions::TravelOwner::TownTrip:
                intent.kind = OverseerDecisions::LeaderIntentKind::TownErrand;
                intent.owner = "town trip";
                return intent;
        }
        intent.kind = OverseerDecisions::LeaderIntentKind::TownErrand;
        intent.owner = "travel column";
        return intent;
    }

    OverseerDecisions::TravelClaimFacts FactsFor(std::string const& name,
                                                 std::string const& target,
                                                 OverseerDecisions::TravelOwner owner,
                                                 std::string const& column,
                                                 bool errandLetGo)
    {
        OverseerDecisions::TravelClaimFacts facts(owner);
        facts.errandLetGo = errandLetGo;
        facts.learnSkill = _column.PendingLearnSkill(name);
        facts.column = column;
        auto const held = _claims.find(name);
        facts.columnIsOurs = held != _claims.end() && held->second.aim == column;
        facts.target = target;
        return facts;
    }

    ReleaseResult ReleaseImpl(std::string const& name, std::string const* expectedRunAim,
                              time_t now, bool inertFollowerAim = false)
    {
        ReleaseResult result;
        result.standing = _column.Read(name);
        auto const held = _claims.find(name);
        result.anyClaim = held != _claims.end();
        bool const claimed = result.anyClaim && held->second.aim == result.standing;
        bool const changedSinceClaim = result.anyClaim && !claimed;
        bool const claimedByRun =
            claimed && OverseerDecisions::TravelOwnerIsALiveRun(held->second.owner);
        if (expectedRunAim &&
            (*expectedRunAim != result.standing || (result.anyClaim && !claimedByRun)))
        {
            result.leftNewerAim = true;
            return result;
        }
        result.endedAim = result.anyClaim ? held->second.aim : result.standing;
        // THE GATE DECIDES (#822): a self-claimed aim whose column has not
        // moved is this module's errand ending, whatever it is; anything else
        // is fenced by what stands there.
        OverseerDecisions::TravelReleaseGateFacts gate;
        gate.claimed = claimed;
        gate.changedSinceClaim = changedSinceClaim;
        gate.learnSkill = claimed ? 0 : _column.PendingLearnSkill(name);
        gate.standing = result.standing;
        gate.inertFollowerAim = inertFollowerAim;
        result.fence = OverseerDecisions::TravelReleaseGate(gate);
        if (result.fence.empty())
            _column.ClearIfStill(name, result.standing);
        _claims.erase(name);
        // STAMPED WHETHER OR NOT ANYTHING WAS OUTSTANDING: unconditional is
        // what makes the stand-down impossible to skip.
        _handback[name] = now;
        return result;
    }

    AimColumn& _column;
    time_t _handbackSeconds;
    std::map<std::string, Held> _claims;
    std::map<std::string, time_t> _handback;
    bool _leaderIntentsEnabled{true};
    OverseerDecisions::LeaderIntentLimits _leaderLimits{};
    std::set<std::string> _leaders;
    std::map<std::string, OverseerDecisions::LeaderIntentState> _leaderStates;
    std::map<std::string, OverseerDecisions::LeaderIntentPreference> _leaderPreferences;
    std::set<std::string> _stayLeakSaid;
};

}  // namespace OverseerMovement

#endif  // OVERSEER_MOVEMENT_OWNERSHIP_H
