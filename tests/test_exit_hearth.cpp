/*
 * A dungeon EXIT that cannot walk the party out hearths it out instead.
 *
 * THE READING THIS EXISTS FOR, measured on the dev realm 2026-09-24. The Horde
 * leader stood alone inside Ragefire Chasm (map 389) with the rest of the
 * family outside. For an hour, every five minutes, the coordinator adopted him
 * at STAGED_INSIDE, set EXIT because his job had left 'dungeon', gave up on the
 * walk to areatrigger 2226, returned to IDLE and adopted him again. The
 * operator got him out with the module's own kind='hearth' row.
 *
 * What is pinned here is who is cast for, who is held still first, who is
 * waited on and who cannot hearth at all; that the coordinator falls back to
 * IDLE and re-adopts only when nobody inside can hearth; and that the hold on
 * adoption has a ceiling.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::EXIT_HEARTH_ATTEMPTS;
using OverseerDecisions::EXIT_HEARTH_EPISODE_SECONDS;
using OverseerDecisions::ExitFailureHearthInPlay;
using OverseerDecisions::ExitFailureHearthStep;
using OverseerDecisions::ExitHearthFacts;
using OverseerDecisions::ExitHearthHoldsAdoption;
using OverseerDecisions::ExitHearthImpossibleReason;
using OverseerDecisions::ExitHearthStep;
using OverseerDecisions::ExitHearthStepWord;

namespace
{

int failures = 0;

void CheckStep(char const* what, ExitHearthStep got, ExitHearthStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, ExitHearthStepWord(got),
                ExitHearthStepWord(want));
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

// The leader as he stood on map 389: alive, standing, out of combat, stone
// ready, nothing in flight.
ExitHearthFacts Stranded()
{
    ExitHearthFacts facts;
    facts.inWorld = true;
    facts.onInsideMap = true;
    facts.alive = true;
    facts.carriesStone = true;
    return facts;
}

}  // namespace

int main()
{
    // -- one member ------------------------------------------------------------
    CheckStep("the stranded leader is cast for", ExitFailureHearthStep(Stranded()),
              ExitHearthStep::Cast);

    {
        ExitHearthFacts outside = Stranded();
        outside.onInsideMap = false;
        CheckStep("a member outside the instance is left alone",
                  ExitFailureHearthStep(outside), ExitHearthStep::NotInside);
        ExitHearthFacts offline = Stranded();
        offline.inWorld = false;
        CheckStep("a member not in the world is left alone", ExitFailureHearthStep(offline),
                  ExitHearthStep::NotInside);
        CheckText("nothing is impossible about a member outside",
                  ExitHearthImpossibleReason(outside), "");
    }

    {
        // kind='hearth' refuses a moving character; the coordinator holds it
        // still first and casts on a later poll.
        ExitHearthFacts moving = Stranded();
        moving.moving = true;
        CheckStep("a moving member is stopped first", ExitFailureHearthStep(moving),
                  ExitHearthStep::StopFirst);
    }

    {
        ExitHearthFacts fighting = Stranded();
        fighting.inCombat = true;
        CheckStep("a member in combat is waited on", ExitFailureHearthStep(fighting),
                  ExitHearthStep::Waiting);
        ExitHearthFacts casting = Stranded();
        casting.pending = true;
        casting.onCooldown = true;  // its own cast put the cooldown on
        CheckStep("a hearth in flight is waited on, not judged by its own cooldown",
                  ExitFailureHearthStep(casting), ExitHearthStep::Waiting);
    }

    {
        ExitHearthFacts cooldown = Stranded();
        cooldown.onCooldown = true;
        CheckStep("a stone on cooldown cannot hearth", ExitFailureHearthStep(cooldown),
                  ExitHearthStep::Impossible);
        CheckText("and says so", ExitHearthImpossibleReason(cooldown),
                  "hearthstone on cooldown");

        ExitHearthFacts noStone = Stranded();
        noStone.carriesStone = false;
        CheckStep("no stone cannot hearth", ExitFailureHearthStep(noStone),
                  ExitHearthStep::Impossible);
        CheckText("and says so", ExitHearthImpossibleReason(noStone),
                  "carries no hearthstone");

        ExitHearthFacts dead = Stranded();
        dead.alive = false;
        CheckStep("a dead member cannot hearth", ExitFailureHearthStep(dead),
                  ExitHearthStep::Impossible);

        // A cast that never starts leaves the cooldown clear; the bound is what
        // stops the same member being asked every poll for ever.
        ExitHearthFacts spent = Stranded();
        spent.attempts = EXIT_HEARTH_ATTEMPTS;
        CheckStep("spent attempts cannot hearth", ExitFailureHearthStep(spent),
                  ExitHearthStep::Impossible);
        spent.attempts = EXIT_HEARTH_ATTEMPTS - 1;
        CheckStep("the last attempt is still cast", ExitFailureHearthStep(spent),
                  ExitHearthStep::Cast);

        // Impossible outranks moving and combat: stopping a character that
        // can never cast is a hold for nothing.
        ExitHearthFacts movingOnCooldown = cooldown;
        movingOnCooldown.moving = true;
        movingOnCooldown.inCombat = true;
        CheckStep("a moving member on cooldown is not held for nothing",
                  ExitFailureHearthStep(movingOnCooldown), ExitHearthStep::Impossible);
    }

    // -- the family ------------------------------------------------------------
    {
        // Ragefire: the leader inside, four outside.
        std::vector<ExitHearthStep> const zug = {
            ExitHearthStep::Cast, ExitHearthStep::NotInside, ExitHearthStep::NotInside,
            ExitHearthStep::NotInside, ExitHearthStep::NotInside};
        CheckBool("one member cast for is a hearth in play", ExitFailureHearthInPlay(zug), true);
        CheckBool("which holds adoption back", ExitHearthHoldsAdoption(zug, 0), true);

        std::vector<ExitHearthStep> const nobody = {
            ExitHearthStep::Impossible, ExitHearthStep::NotInside, ExitHearthStep::Impossible};
        CheckBool("nobody able to hearth falls back to IDLE", ExitFailureHearthInPlay(nobody),
                  false);
        CheckBool("and adoption goes on as before", ExitHearthHoldsAdoption(nobody, 0), false);

        std::vector<ExitHearthStep> const allOut = {ExitHearthStep::NotInside,
                                                    ExitHearthStep::NotInside};
        CheckBool("nobody inside holds nothing", ExitHearthHoldsAdoption(allOut, 0), false);

        std::vector<ExitHearthStep> const held = {ExitHearthStep::StopFirst,
                                                  ExitHearthStep::Impossible};
        CheckBool("a member being stopped still holds adoption",
                  ExitHearthHoldsAdoption(held, 60), true);
        std::vector<ExitHearthStep> const waiting = {ExitHearthStep::Waiting};
        CheckBool("a member waited on still holds adoption",
                  ExitHearthHoldsAdoption(waiting, EXIT_HEARTH_EPISODE_SECONDS - 1), true);
        CheckBool("but never past the episode's ceiling",
                  ExitHearthHoldsAdoption(waiting, EXIT_HEARTH_EPISODE_SECONDS), false);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_exit_hearth: all passed\n");
    return 0;
}
