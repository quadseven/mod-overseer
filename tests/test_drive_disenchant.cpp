/*
 * DriveDisenchant's decisions (#444): which owned item is disenchanted, when
 * the errand stands, and when it is done.
 *
 * "delivered is not done" (AGENTS.md): the errand is done when a reagent the
 * disenchant loot window held is counted in the bags above what the bags held
 * before the cast, and not when the cast was accepted. The facts below are the
 * ones DriveDisenchant reads off the character each poll (GetItemCount for the
 * reagent counts, GetLootGUID for the window, the cast answer from CastSpell);
 * the reagent entries are the ones a Strange Dust window holds (item 10940).
 *
 * Compiled against src/overseer_decisions.cpp.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::DecideDisenchant;
using OverseerDecisions::DisenchantCandidate;
using OverseerDecisions::DisenchantErrandStands;
using OverseerDecisions::DisenchantFacts;
using OverseerDecisions::DisenchantReagent;
using OverseerDecisions::DisenchantReagentSeen;
using OverseerDecisions::DisenchantResultJson;
using OverseerDecisions::DisenchantRowStatus;
using OverseerDecisions::DisenchantStep;
using OverseerDecisions::DisenchantStepWord;
using OverseerDecisions::IsDisenchantRequest;
using OverseerDecisions::PickDisenchantItem;

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

void Expect(char const* what, DisenchantStep got, DisenchantStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: expected %s, got %s\n", what, DisenchantStepWord(want),
                DisenchantStepWord(got));
    ++failures;
}

constexpr std::uint32_t STRANGE_DUST = 10940;

DisenchantCandidate SoulboundGreenNotWearable()
{
    DisenchantCandidate c;
    c.quality = OverseerDecisions::ITEM_QUALITY_GREEN;
    c.soulbound = true;
    c.wearable = false;
    c.disenchantable = true;
    return c;
}

// A character standing still in town with a soulbound green in its bags and
// nothing cast yet.
DisenchantFacts Ready()
{
    DisenchantFacts f;
    f.present = true;
    f.able = true;
    f.hasItem = true;
    return f;
}

void ThePickIsASoulboundGreenTheCharacterCannotWear()
{
    std::vector<DisenchantCandidate> bags(1, SoulboundGreenNotWearable());
    Check("a soulbound green the character cannot wear is picked", PickDisenchantItem(bags) == 0);

    DisenchantCandidate wearable = SoulboundGreenNotWearable();
    wearable.wearable = true;
    DisenchantCandidate bindOnEquip = SoulboundGreenNotWearable();
    bindOnEquip.soulbound = false;
    DisenchantCandidate blue = SoulboundGreenNotWearable();
    blue.quality = 3;
    DisenchantCandidate notByTheCore = SoulboundGreenNotWearable();
    notByTheCore.disenchantable = false;
    DisenchantCandidate refused = SoulboundGreenNotWearable();
    refused.refused = true;
    for (DisenchantCandidate const& c : {wearable, bindOnEquip, blue, notByTheCore, refused})
        Check("only a soulbound green the character cannot wear, which the core would "
              "disenchant and which was not refused already, is picked",
              PickDisenchantItem(std::vector<DisenchantCandidate>(1, c)) == -1);

    std::vector<DisenchantCandidate> mixed = {wearable, refused, SoulboundGreenNotWearable()};
    Check("the first eligible item in bag order is picked", PickDisenchantItem(mixed) == 2);
    Check("empty bags pick nothing", PickDisenchantItem({}) == -1);
}

void TheReagentIsReadOutOfTheBags()
{
    Check("no reagent noted is not a reagent seen", !DisenchantReagentSeen({}));
    DisenchantReagent dust;
    dust.entry = STRANGE_DUST;
    dust.before = 4;
    dust.now = 4;
    Check("a count that did not rise is not a reagent seen",
          !DisenchantReagentSeen(std::vector<DisenchantReagent>(1, dust)));
    dust.now = 6;
    Check("a count above the count before the cast is a reagent seen",
          DisenchantReagentSeen(std::vector<DisenchantReagent>(1, dust)));
    dust.now = 3;
    Check("a count that fell (the character used some) is not a reagent seen",
          !DisenchantReagentSeen(std::vector<DisenchantReagent>(1, dust)));
}

// ACCEPTANCE: the reagent lands in the bags, verified by querying inventory
// after the cast and not by trusting the cast result.
void AnAcceptedCastIsNotDoneUntilTheBagsSaySo()
{
    DisenchantFacts f = Ready();
    f.castsSpent = 1;  // CastSpell answered SPELL_CAST_OK on an earlier poll
    DisenchantReagent dust;
    dust.entry = STRANGE_DUST;
    dust.before = 0;
    dust.now = 0;
    f.reagentSeen = DisenchantReagentSeen(std::vector<DisenchantReagent>(1, dust));
    Check("an accepted cast with nothing in the bags is not done",
          DecideDisenchant(f) != DisenchantStep::Done);
    Check("and, with another green carried, its errand still stands",
          DisenchantErrandStands(DecideDisenchant(f)));
    DisenchantFacts lastGreen = f;
    lastGreen.hasItem = false;
    Expect("with no green left it ends as nothing to disenchant, never as done",
           DecideDisenchant(lastGreen), DisenchantStep::NoItem);

    DisenchantFacts inFlight = f;
    inFlight.castsSpent = OverseerDecisions::DISENCHANT_CASTS_ALLOWED;
    inFlight.casting = true;
    Expect("the last cast still in flight is waited for, not given up on",
           DecideDisenchant(inFlight), DisenchantStep::Wait);

    dust.now = 2;
    f.reagentSeen = DisenchantReagentSeen(std::vector<DisenchantReagent>(1, dust));
    Expect("two Strange Dust counted in the bags after the cast is done", DecideDisenchant(f),
           DisenchantStep::Done);
    Check("a done errand is cleared", !DisenchantErrandStands(DisenchantStep::Done));
    Check("and its row ends applied",
          std::string(DisenchantRowStatus(DisenchantStep::Done)) == "applied");
}

// ACCEPTANCE: the errand stands (is not cleared) on a transient refusal,
// matching DriveCraft.
void ATransientRefusalLeavesTheErrandStanding()
{
    DisenchantFacts inCombat = Ready();
    inCombat.able = false;
    DisenchantFacts moving = Ready();
    moving.moving = true;
    DisenchantFacts casting = Ready();
    casting.casting = true;
    DisenchantFacts otherWindow = Ready();
    otherWindow.otherLootOpen = true;
    DisenchantFacts gone = Ready();
    gone.present = false;
    for (DisenchantFacts const& f : {inCombat, moving, casting, otherWindow, gone})
    {
        Expect("a character that cannot cast this poll waits", DecideDisenchant(f),
               DisenchantStep::Wait);
        Check("and the errand stands", DisenchantErrandStands(DecideDisenchant(f)));
    }

    // CastSpell refused for a reason that passes (not ready, a global
    // cooldown): castsRefused rose, castsSpent did not, and the next poll
    // casts again.
    DisenchantFacts refusedOnce = Ready();
    refusedOnce.castsRefused = 1;
    Expect("after a transient refusal the next poll casts again", DecideDisenchant(refusedOnce),
           DisenchantStep::Cast);
    Check("so the errand stands", DisenchantErrandStands(DecideDisenchant(refusedOnce)));
    Check("a standing errand writes no row status",
          std::string(DisenchantRowStatus(DisenchantStep::Wait)).empty() &&
              std::string(DisenchantRowStatus(DisenchantStep::Cast)).empty());
}

void TheWindowOnTheItemIsTakenBeforeAnythingElse()
{
    DisenchantFacts f = Ready();
    f.castsSpent = 1;
    f.lootOnTarget = true;
    f.able = false;  // a mob walked up while the window was open
    Expect("an open disenchant window is taken", DecideDisenchant(f), DisenchantStep::TakeLoot);
    Check("and the errand stands until the bags are read", DisenchantErrandStands(DecideDisenchant(f)));
}

void TheErrandEnds()
{
    DisenchantFacts none = Ready();
    none.hasItem = false;
    Expect("no item to disenchant ends the errand", DecideDisenchant(none), DisenchantStep::NoItem);
    Check("and clears it", !DisenchantErrandStands(DisenchantStep::NoItem));
    Check("and the row ends unchanged",
          std::string(DisenchantRowStatus(DisenchantStep::NoItem)) == "unchanged");

    DisenchantFacts spent = Ready();
    spent.castsSpent = OverseerDecisions::DISENCHANT_CASTS_ALLOWED;
    Expect("the cast budget spent with nothing seen gives up", DecideDisenchant(spent),
           DisenchantStep::GiveUp);
    DisenchantFacts late = Ready();
    late.waitedMs = OverseerDecisions::DISENCHANT_WINDOW_MS;
    late.able = false;
    Expect("the window run out gives up even while waiting", DecideDisenchant(late),
           DisenchantStep::GiveUp);
    Check("a give-up row ends unchanged",
          std::string(DisenchantRowStatus(DisenchantStep::GiveUp)) == "unchanged");

    DisenchantFacts seenLate = late;
    seenLate.reagentSeen = true;
    Expect("a reagent seen at the end of the window still counts", DecideDisenchant(seenLate),
           DisenchantStep::Done);
}

void TheRequestAndTheResultAreClosedVocabularies()
{
    Check("the fixed rule's word is a request", IsDisenchantRequest("soulbound-green"));
    Check("surrounding blanks are tolerated", IsDisenchantRequest("  soulbound-green "));
    Check("anything else is not", !IsDisenchantRequest("item:1234") && !IsDisenchantRequest(""));

    OverseerDecisions::DisenchantEvidence ev;
    ev.itemEntry = 2075;
    ev.castsSpent = 1;
    ev.reagentEntry = STRANGE_DUST;
    ev.reagentGained = 2;
    std::string const json = DisenchantResultJson(ev, DisenchantStep::Done);
    Check("the result names the outcome", json.find("\"outcome\":\"done\"") != std::string::npos);
    Check("the result carries the reagent read back",
          json.find("\"reagent_entry\":10940") != std::string::npos &&
              json.find("\"reagent_gained\":2") != std::string::npos);
    bool onlySafe = true;
    for (char const c : json)
        onlySafe = onlySafe && c != '\'' && c != '\\';
    for (DisenchantStep const step : {DisenchantStep::Done, DisenchantStep::NoItem,
                                      DisenchantStep::GiveUp, DisenchantStep::Wait})
        for (char const* c = OverseerDecisions::DisenchantRowDetail(step); *c; ++c)
            onlySafe = onlySafe && *c != '\'' && *c != '\\';
    Check("a finished row says why it ended",
          *OverseerDecisions::DisenchantRowDetail(DisenchantStep::NoItem) != '\0' &&
              *OverseerDecisions::DisenchantRowDetail(DisenchantStep::GiveUp) != '\0');
    Check("the result holds no quote or backslash a SQL literal could be broken by", onlySafe);
}
}  // namespace

int main()
{
    ThePickIsASoulboundGreenTheCharacterCannotWear();
    TheReagentIsReadOutOfTheBags();
    AnAcceptedCastIsNotDoneUntilTheBagsSaySo();
    ATransientRefusalLeavesTheErrandStanding();
    TheWindowOnTheItemIsTakenBeforeAnythingElse();
    TheErrandEnds();
    TheRequestAndTheResultAreClosedVocabularies();
    if (failures)
        return 1;
    std::puts("ok test_drive_disenchant");
    return 0;
}
