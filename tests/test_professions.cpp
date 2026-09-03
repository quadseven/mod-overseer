/*
 * The profession assignment, decided without a world.
 *
 * WHY THIS FILE IS THE FIRST OF ITS KIND HERE. src/overseer_decisions.h was
 * split out of the big translation unit for one stated reason - "a test - or a
 * reader - can compile these two on their own" - and until now nothing in this
 * repository actually did. The seam was real and unreached, which is the same
 * shape of problem as a config option nothing reads.
 *
 * It compiles the pure file and this one, and NOTHING ELSE. If a core type ever
 * gets into a decision, this stops linking, which is the property the header
 * says it is protecting.
 *
 * WHAT IT PINS. Mostly one thing, because one thing is worth far more than the
 * rest: that a character already holding what it was assigned is left alone.
 * Giving up a primary profession destroys every point of it and there is no
 * undo, so "re-running the assignment does not unlearn and relearn what is
 * already correct" is not a nicety - it is the difference between a drive that
 * converges and one that grinds a family's work to nothing a poll at a time.
 * The cases below walk each family shape from where it actually stands to where
 * the roster says it should be, and then keep polling to prove it stops.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::NextProfessionStep;
using OverseerDecisions::ProfessionHolding;
using OverseerDecisions::ProfessionStep;
using OverseerDecisions::ProfessionStepKind;

namespace
{

// The skill ids this module already prints beside every name it logs. Spelled
// here so a failure reads as a profession rather than as a number.
unsigned const BLACKSMITHING = 164;
unsigned const LEATHERWORKING = 165;
unsigned const ALCHEMY = 171;
unsigned const HERBALISM = 182;
unsigned const MINING = 186;
unsigned const TAILORING = 197;
unsigned const ENCHANTING = 333;
unsigned const SKINNING = 393;

int failures = 0;

char const* KindName(ProfessionStepKind kind)
{
    switch (kind)
    {
        case ProfessionStepKind::Nothing: return "Nothing";
        case ProfessionStepKind::GiveUp:  return "GiveUp";
        case ProfessionStepKind::Take:    return "Take";
    }
    return "?";
}

void Expect(char const* what, ProfessionStep const& got, ProfessionStepKind kind,
            unsigned skill, unsigned cost)
{
    if (got.kind == kind && got.skill == skill && got.cost == cost)
        return;

    ++failures;
    std::printf("FAIL %s: expected %s skill %u cost %u, got %s skill %u cost %u\n",
                what, KindName(kind), skill, cost,
                KindName(got.kind), got.skill, got.cost);
}

ProfessionHolding Held(unsigned skill, unsigned value)
{
    ProfessionHolding holding;
    holding.skill = skill;
    holding.value = value;
    return holding;
}

// One character's whole journey: poll, apply what the step says to the held
// set, poll again, until it says Nothing. Returns how many steps that took, so
// a test can assert the sequence terminated rather than merely that each step
// looked right.
//
// APPLYING THE STEP IS WHAT MAKES THIS A TEST OF CONVERGENCE rather than of one
// answer. A Take adds the skill at value 1, which is what a trainer sells; a
// GiveUp removes it entirely, which is what SetSkill(id, 0, 0, 0) does.
unsigned RunToStillness(std::vector<unsigned> const& wanted,
                        std::vector<ProfessionHolding> held, unsigned maxPrimary,
                        std::vector<ProfessionHolding>& finalHeld)
{
    unsigned steps = 0;
    for (; steps < 20; ++steps)
    {
        ProfessionStep const step = NextProfessionStep(wanted, held, maxPrimary);
        if (step.kind == ProfessionStepKind::Nothing)
            break;

        if (step.kind == ProfessionStepKind::Take)
        {
            held.push_back(Held(step.skill, 1));
            continue;
        }

        for (std::vector<ProfessionHolding>::iterator it = held.begin();
             it != held.end(); ++it)
        {
            if (it->skill != step.skill)
                continue;
            held.erase(it);
            break;
        }
    }
    finalHeld = held;
    return steps;
}

void ExpectHolds(char const* what, std::vector<ProfessionHolding> const& held,
                 std::vector<unsigned> const& expected)
{
    bool same = held.size() == expected.size();
    for (unsigned const skill : expected)
    {
        bool found = false;
        for (ProfessionHolding const& holding : held)
            if (holding.skill == skill)
                found = true;
        if (!found)
            same = false;
    }
    if (same)
        return;

    ++failures;
    std::printf("FAIL %s: ended holding", what);
    for (ProfessionHolding const& holding : held)
        std::printf(" %u(%u)", holding.skill, holding.value);
    std::printf(", expected");
    for (unsigned const skill : expected)
        std::printf(" %u", skill);
    std::printf("\n");
}

// THE ONE THAT MATTERS MOST. A character already holding exactly what it was
// assigned must get Nothing, and must go on getting Nothing however many times
// it is asked - because it IS asked, on every poll, for the rest of its life.
// The holding here is the largest skill value anybody on the roster has ever
// accumulated, so if this rule is ever broken this is what it costs.
void AlreadyCorrectIsNeverTouched()
{
    std::vector<unsigned> const wanted = {ALCHEMY, HERBALISM};
    std::vector<ProfessionHolding> const held = {Held(ALCHEMY, 1), Held(HERBALISM, 117)};

    for (int poll = 0; poll < 100; ++poll)
        Expect("already correct", NextProfessionStep(wanted, held, 2),
               ProfessionStepKind::Nothing, 0, 0);
}

// The same character, asked for something that cannot fit. Every slot is full
// of skills the roster ALSO wants, so there is no candidate to give up, and the
// answer is to do nothing rather than to pick a victim. Without this rule the
// oversized row would cost 117 points and still not be satisfiable.
void AnImpossibleAssignmentDestroysNothing()
{
    std::vector<unsigned> const wanted = {ALCHEMY, HERBALISM, TAILORING};
    std::vector<ProfessionHolding> const held = {Held(ALCHEMY, 1), Held(HERBALISM, 117)};

    Expect("assignment too big", NextProfessionStep(wanted, held, 2),
           ProfessionStepKind::Nothing, 0, 0);
}

// No decision recorded means no permission to do anything, however wrong what
// the character holds might look. An absent opinion is not a licence.
void NoOpinionMeansNoChange()
{
    std::vector<ProfessionHolding> const held = {Held(ALCHEMY, 1), Held(HERBALISM, 40)};

    Expect("no opinion", NextProfessionStep({}, held, 2),
           ProfessionStepKind::Nothing, 0, 0);
}

// A free slot is filled before anything is destroyed, so nobody is ever left
// holding nothing while a trainer queue drains.
void RoomBeforeRuin()
{
    std::vector<unsigned> const wanted = {TAILORING, ENCHANTING};
    std::vector<ProfessionHolding> const held = {Held(HERBALISM, 30)};

    Expect("free slot first", NextProfessionStep(wanted, held, 2),
           ProfessionStepKind::Take, TAILORING, 0);
}

// The cheapest thing the roster did not ask for is what goes, and the id is the
// tie-break so two polls can never disagree about which of two equal skills
// dies.
void TheCheapestUnwantedThingGoes()
{
    std::vector<unsigned> const wanted = {MINING, BLACKSMITHING};

    Expect("cheapest goes",
           NextProfessionStep(wanted, {Held(HERBALISM, 15), Held(ALCHEMY, 1)}, 2),
           ProfessionStepKind::GiveUp, ALCHEMY, 1);

    Expect("ties break on id",
           NextProfessionStep(wanted, {Held(HERBALISM, 1), Held(ALCHEMY, 1)}, 2),
           ProfessionStepKind::GiveUp, ALCHEMY, 1);
}

// Gathering before crafting, which the ids sort the wrong way round for in both
// of the pairs the family actually holds.
void TheSupplyIsTakenBeforeTheCraft()
{
    Expect("mining before blacksmithing",
           NextProfessionStep({BLACKSMITHING, MINING}, {}, 2),
           ProfessionStepKind::Take, MINING, 0);

    Expect("skinning before leatherworking",
           NextProfessionStep({LEATHERWORKING, SKINNING}, {}, 2),
           ProfessionStepKind::Take, SKINNING, 0);
}

// Every shape on the family roster, walked from where it stands today to where
// it is assigned, and then polled until it stops. The point is the LAST
// assertion in each: the sequence ends, and it ends holding the assigned pair.
void TheWholeFamilyConverges()
{
    struct Case
    {
        char const* who;
        std::vector<unsigned> wanted;
        std::vector<ProfessionHolding> held;
        unsigned steps;
    };

    Case const cases[] = {
        // Two primaries held, neither assigned: both go, one at a time, and
        // each one leaves through a slot the next skill is taken into.
        {"the miner-smith", {BLACKSMITHING, MINING},
         {Held(ALCHEMY, 1), Held(HERBALISM, 15)}, 4},
        {"the skinner", {LEATHERWORKING, SKINNING},
         {Held(ALCHEMY, 1), Held(HERBALISM, 7)}, 4},
        // One primary held, not assigned, and a slot already free: the free
        // slot is used first, so this character is never professionless.
        {"the enchanter", {TAILORING, ENCHANTING},
         {Held(HERBALISM, 30)}, 3},
        // Nothing held at all.
        {"a new character", {HERBALISM, ALCHEMY}, {}, 2},
        // Already right: no steps at all.
        {"the herbalist", {ALCHEMY, HERBALISM},
         {Held(ALCHEMY, 1), Held(HERBALISM, 117)}, 0},
    };

    for (Case const& one : cases)
    {
        std::vector<ProfessionHolding> ended;
        unsigned const steps = RunToStillness(one.wanted, one.held, 2, ended);
        if (steps != one.steps)
        {
            ++failures;
            std::printf("FAIL %s: converged in %u steps, expected %u\n",
                        one.who, steps, one.steps);
        }
        ExpectHolds(one.who, ended, one.wanted);

        // AND IT STAYS THERE. The poll after the last one is the whole
        // acceptance criterion.
        Expect(one.who, NextProfessionStep(one.wanted, ended, 2),
               ProfessionStepKind::Nothing, 0, 0);
    }
}

// A character that has reached its assignment must not lose it to a poll that
// happens to see it holding something the roster has no opinion about - which
// is what "held == wanted" would have done, and is why the rule is written
// against the missing half only.
void SomethingExtraIsNotTidiedAway()
{
    std::vector<unsigned> const wanted = {HERBALISM};
    std::vector<ProfessionHolding> const held = {Held(HERBALISM, 117), Held(ALCHEMY, 1)};

    Expect("extra is left alone", NextProfessionStep(wanted, held, 2),
           ProfessionStepKind::Nothing, 0, 0);
}

}  // namespace

int main()
{
    AlreadyCorrectIsNeverTouched();
    AnImpossibleAssignmentDestroysNothing();
    NoOpinionMeansNoChange();
    RoomBeforeRuin();
    TheCheapestUnwantedThingGoes();
    TheSupplyIsTakenBeforeTheCraft();
    TheWholeFamilyConverges();
    SomethingExtraIsNotTidiedAway();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the profession decisions hold\n");
    return EXIT_SUCCESS;
}
