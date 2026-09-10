/*
 * Casting a spell on purpose, and what a row is allowed to claim afterwards.
 *
 * mod-overseer#408. Nothing in this module could make a character cast. The
 * queue's `kind='bot'` looked like it could and never was: it is the
 * fall-through into PlayerbotAI::HandleCommand, every row of that kind this
 * realm has carried is a strategy toggle, and a row reading `cast 10059` was
 * accepted, cast nothing and was answered `delivered`.
 *
 * That mattered more than its size because #395 had already enumerated every
 * mechanic in the pinned build that moves this roster from map 1 to map 0 and
 * found exactly one: the party's mage casting Portal: Stormwind, spell 10059,
 * whose object is party only and therefore carries everybody.
 *
 * What is pinned here is the grammar, which claims the readings support, how
 * long to wait before judging, and what a sender should do about each refusal.
 * The numbers a caller passes in are the executor's and are named there; this
 * file pins the arithmetic over them, so a failure reads as the rule and not as
 * an arbitrary constant.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CastOutcome;
using OverseerDecisions::CastOutcomeWord;
using OverseerDecisions::CastPlaceChanged;
using OverseerDecisions::CastReadBack;
using OverseerDecisions::CastRefusalRetry;
using OverseerDecisions::CastRequest;
using OverseerDecisions::CastTargetKind;
using OverseerDecisions::CastVerifyWindowMs;
using OverseerDecisions::HomeBind;
using OverseerDecisions::JudgeCast;
using OverseerDecisions::ParseCastRequest;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;

namespace CastRefusal = OverseerDecisions::CastRefusal;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNum(char const* what, uint32_t got, uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, unsigned(got), unsigned(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
    ++failures;
}

void CheckOutcome(char const* what, CastOutcome got, CastOutcome want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, CastOutcomeWord(got),
                CastOutcomeWord(want));
    ++failures;
}

void CheckRetry(char const* what, std::string const& detail, TownRetry want)
{
    TownRetry const got = CastRefusalRetry(detail);
    if (got == want)
        return;
    std::printf("FAIL %s (%s): got %s, wanted %s\n", what, detail.c_str(),
                TownRetryWord(got), TownRetryWord(want));
    ++failures;
}

// ----------------------------------------------------------------- grammar --

// THE ROW THE WHOLE ISSUE IS ABOUT. A bare spell id is the portal, and it is
// the shortest thing a sender can write.
void ABareSpellIdIsACastOnSelf()
{
    CastRequest const r = ParseCastRequest("10059");
    Check("bare id parses", r.ok, true);
    CheckNum("bare id is the spell", r.spellId, 10059);
    Check("bare id targets self", r.target == CastTargetKind::Self, true);
    Check("bare id names nobody", r.targetName.empty(), true);
}

void WhitespaceAroundTheRowIsNotAnError()
{
    CastRequest const r = ParseCastRequest("   10059\t ");
    Check("padded id parses", r.ok, true);
    CheckNum("padded id is the spell", r.spellId, 10059);
}

void OnSelfIsTheBareFormSaidOutLoud()
{
    CastRequest const r = ParseCastRequest("3561 on:self");
    Check("on:self parses", r.ok, true);
    CheckNum("on:self keeps the spell", r.spellId, 3561);
    Check("on:self targets self", r.target == CastTargetKind::Self, true);
    Check("on:self names nobody", r.targetName.empty(), true);

    // Said in any case, because a sender writing `on:Self` has not made a
    // different request.
    CastRequest const upper = ParseCastRequest("3561 on:SELF");
    Check("on:SELF parses", upper.ok, true);
    Check("on:SELF targets self", upper.target == CastTargetKind::Self, true);
}

void ANamedTargetIsCarriedThrough()
{
    CastRequest const r = ParseCastRequest("1459 on:Fenwick");
    Check("named target parses", r.ok, true);
    CheckNum("named target keeps the spell", r.spellId, 1459);
    Check("named target is Named", r.target == CastTargetKind::Named, true);
    Check("the name is carried verbatim", r.targetName == "Fenwick", true);
}

// A NAME IS NOT A SPELL ID, AND THIS IS THE MISTAKE THE VERB EXPECTS. The row
// that started #408 read `cast portal: stormwind`, and the answer to it must be
// a sentence rather than a shrug.
void ASpellNameIsRefusedWithASentence()
{
    CastRequest const r = ParseCastRequest("portal:");
    Check("a name does not parse", r.ok, false);
    CheckWord("a name is named as a name", r.error, CastRefusal::NotANumber);

    CastRequest const two = ParseCastRequest("portal: stormwind");
    Check("a two word name does not parse", two.ok, false);
    // Two words with a non-numeric first word: the first word is still the
    // thing that is wrong, and saying so points at the fix.
    CheckWord("a two word name is named as a name", two.error,
              CastRefusal::NotANumber);
}

void AnEmptyRowIsRefusedRatherThanGuessedAt()
{
    CastRequest const r = ParseCastRequest("");
    Check("empty does not parse", r.ok, false);
    CheckWord("empty says what is missing", r.error, CastRefusal::NoSpellId);

    CastRequest const blank = ParseCastRequest("   ");
    Check("whitespace does not parse", blank.ok, false);
    CheckWord("whitespace says what is missing", blank.error,
              CastRefusal::NoSpellId);
}

// ZERO IS A LEGAL RUN OF DIGITS AND IS NOT A SPELL. Left to `ReadSpellId` it
// would parse cleanly and be refused two hundred lines later by the core, with
// no sentence attached.
void SpellIdZeroIsNotSilentlyAccepted()
{
    CastRequest const r = ParseCastRequest("0");
    Check("zero does not parse", r.ok, false);
    CheckWord("zero is named", r.error, CastRefusal::NotANumber);
}

// SATURATION IS NOT ENOUGH HERE, WHICH IS WHY THE LENGTH IS CHECKED. A run of
// digits that overflows would wrap into a small number and cast something the
// sender never asked for, which is the one parse failure with a live cost.
void AnAbsurdSpellIdIsRefusedRatherThanWrapped()
{
    CastRequest const r = ParseCastRequest("99999999999999");
    Check("an absurd id does not parse", r.ok, false);
    CheckWord("an absurd id is named apart from a name", r.error,
              CastRefusal::SpellIdTooBig);

    // ...and the largest thing that IS accepted still parses, so the fence is
    // not sitting on top of real spell ids. Nine digits is four orders of
    // magnitude above anything in the DBC.
    CastRequest const big = ParseCastRequest("999999999");
    Check("nine digits still parses", big.ok, true);
    CheckNum("nine digits is read exactly", big.spellId, 999999999u);
}

void ATrailingWordThatIsNotATargetIsRefused()
{
    CastRequest const r = ParseCastRequest("10059 now");
    Check("a stray word does not parse", r.ok, false);
    CheckWord("a stray word gets the grammar", r.error, CastRefusal::BadTarget);

    CastRequest const three = ParseCastRequest("10059 on:Fenwick please");
    Check("three words do not parse", three.ok, false);
    CheckWord("three words get the grammar", three.error, CastRefusal::BadTarget);

    // `on:` with nothing after it is the same mistake and must not become a
    // request to cast at a character called nothing.
    CastRequest const empty = ParseCastRequest("10059 on:");
    Check("an empty target does not parse", empty.ok, false);
    CheckWord("an empty target gets the grammar", empty.error,
              CastRefusal::BadTarget);
}

void ATargetThatCannotBeACharacterNameIsRefusedAsOne()
{
    CastRequest const r = ParseCastRequest("10059 on:Fen wick");
    Check("a spaced name does not parse", r.ok, false);
    // Three words, so it is the grammar that is wrong rather than the name.
    CheckWord("a spaced name gets the grammar", r.error, CastRefusal::BadTarget);

    CastRequest const digits = ParseCastRequest("10059 on:12345");
    Check("a numeric name does not parse", digits.ok, false);
    CheckWord("a numeric name is named as a name", digits.error,
              CastRefusal::BadTargetName);
}

// ---------------------------------------------------------------- verdicts --

// THE PORTAL, WHICH IS THE ROW THIS VERB WAS BUILT FOR. The object the spell
// creates is standing there, owned by this caster.
void AnObjectThatIsStandingThereIsMade()
{
    CastReadBack read;
    read.objectExpected = true;
    read.objectFound = true;
    CheckOutcome("a portal that exists is made", JudgeCast(read), CastOutcome::Made);
}

// AND THE FAILURE IT EXISTS TO MAKE VISIBLE. A cast that went out, a window
// that elapsed, and no object. Not `delivered`, not `spent`, and not silence.
void AnObjectThatIsNotThereIsNothing()
{
    CastReadBack read;
    read.objectExpected = true;
    read.objectFound = false;
    CheckOutcome("a portal that is absent is nothing", JudgeCast(read),
                 CastOutcome::Nothing);
}

// THE ORDER IS THE POINT. A portal takes a Rune of Portals, so a cast that
// failed after taking the reagent has `costPaid` true. Judging on the cost
// would report `spent` about a row whose whole purpose failed.
void TheObjectBeatsTheCostThatWasAlsoPaid()
{
    CastReadBack read;
    read.objectExpected = true;
    read.objectFound = false;
    read.costExpected = true;
    read.costPaid = true;
    CheckOutcome("a spent reagent does not rescue a missing portal", JudgeCast(read),
                 CastOutcome::Nothing);

    read.objectFound = true;
    CheckOutcome("and a found portal is still made", JudgeCast(read), CastOutcome::Made);
}

void ASelfTeleportIsJudgedOnWhereTheCharacterIs()
{
    CastReadBack read;
    read.teleportExpected = true;
    read.placeChanged = true;
    CheckOutcome("a teleport that moved is moved", JudgeCast(read), CastOutcome::Moved);

    read.placeChanged = false;
    CheckOutcome("a teleport that did not move is nothing", JudgeCast(read),
                 CastOutcome::Nothing);
}

// AND AN OBJECT SPELL THAT ALSO TELEPORTS IS JUDGED ON THE OBJECT, for the same
// reason the cost does not win: the object is the more specific reading.
void TheObjectBeatsTheTeleport()
{
    CastReadBack read;
    read.objectExpected = true;
    read.objectFound = false;
    read.teleportExpected = true;
    read.placeChanged = true;
    CheckOutcome("a moved caster does not rescue a missing object", JudgeCast(read),
                 CastOutcome::Nothing);
}

// EVERY OTHER SPELL. There is no object and nobody moved, so what the cast COST
// is the only post-condition left, and it is a real one: a reagent, power and a
// cooldown are all taken by a cast that completed and by nothing else.
void ASpellWithNoObjectAndNoTeleportIsJudgedOnWhatItCost()
{
    CastReadBack read;
    read.costExpected = true;
    read.costPaid = true;
    CheckOutcome("a cast that cost something is spent", JudgeCast(read),
                 CastOutcome::Spent);

    read.costPaid = false;
    CheckOutcome("a cast that cost nothing is nothing", JudgeCast(read),
                 CastOutcome::Nothing);
}

// THE HONEST UNKNOWN, AND IT IS NOT `nothing`. A free instant spell with no
// cooldown leaves no trace this module can read, so no reading afterwards can
// separate a cast that worked from one that never started. Claiming `nothing`
// there would be asserting a failure that was never measured.
void ASpellThatLeavesNoTraceIsUnreadableAndNotAFailure()
{
    CastReadBack read;
    CheckOutcome("no post-condition at all is unreadable", JudgeCast(read),
                 CastOutcome::Unreadable);
}

// A CASTER THAT LEFT THE WORLD ANSWERS NOTHING ELSE, whatever else was true.
void ACasterThatCannotBeReadIsUnreadable()
{
    CastReadBack read;
    read.casterReadable = false;
    read.objectExpected = true;
    read.objectFound = true;
    CheckOutcome("an unreadable caster outranks a found object", JudgeCast(read),
                 CastOutcome::Unreadable);
}

void TheOutcomeWordsAreTheWordsARowCarries()
{
    CheckWord("made", CastOutcomeWord(CastOutcome::Made), "made");
    CheckWord("moved", CastOutcomeWord(CastOutcome::Moved), "moved");
    CheckWord("spent", CastOutcomeWord(CastOutcome::Spent), "spent");
    CheckWord("nothing", CastOutcomeWord(CastOutcome::Nothing), "nothing");
    CheckWord("unreadable", CastOutcomeWord(CastOutcome::Unreadable), "unreadable");
}

// ------------------------------------------------------------ did it move? --

HomeBind Place(uint32_t map, float x, float y, float z)
{
    HomeBind p;
    p.known = true;
    p.mapId = map;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

// THE CROSSING THIS FAMILY NEEDS, AND THE ONE COMPARISON THAT MUST NOT FORGET
// THE MAP. Map 0 and map 1 share a coordinate space, so identical coordinates
// on different maps is a continent crossed, not a character standing still.
void ADifferentMapIsAMoveWhateverTheCoordinatesSay()
{
    HomeBind const from = Place(1, -900.f, -3800.f, 10.f);
    HomeBind const now = Place(0, -900.f, -3800.f, 10.f);
    Check("a map change is a move", CastPlaceChanged(from, now, 10.f), true);
}

void APaceOfDriftIsNotATeleport()
{
    HomeBind const from = Place(1, 0.f, 0.f, 0.f);
    HomeBind const near = Place(1, 3.f, 4.f, 0.f);  // 5 yards
    Check("five yards of drift is not a move", CastPlaceChanged(from, near, 10.f), false);

    HomeBind const far = Place(1, 30.f, 40.f, 0.f);  // 50 yards
    Check("fifty yards is a move", CastPlaceChanged(from, far, 10.f), true);
}

// THE THIRD DIMENSION COUNTS. A character teleported straight up a shaft has
// moved, and a comparison that only looked at x and y would call it stationary.
void HeightCountsTowardTheDistance()
{
    HomeBind const from = Place(1, 0.f, 0.f, 0.f);
    HomeBind const up = Place(1, 0.f, 0.f, 40.f);
    Check("straight up is a move", CastPlaceChanged(from, up, 10.f), true);
}

// AN UNREAD PLACE IS NOT A MOVE. `false` here feeds `Nothing` rather than
// `Moved`, which is the safe direction: a row must never claim a crossing it
// could not measure.
void AnUnreadPlaceIsNeverAMove()
{
    HomeBind const known = Place(1, 0.f, 0.f, 0.f);
    HomeBind unknown;
    Check("an unread start is not a move", CastPlaceChanged(unknown, known, 10.f), false);
    Check("an unread end is not a move", CastPlaceChanged(known, unknown, 10.f), false);
}

// ------------------------------------------------------------- the window --

void TheWindowIsTheCastPlusTheMargin()
{
    // Portal: Stormwind is a ten second cast at the pinned build, which is the
    // number this whole window exists to absorb.
    CheckNum("ten seconds plus five", CastVerifyWindowMs(10000, 5000, 8000, 40000), 15000);
}

// A CAST TIME THAT READS AS ZERO MUST NOT JUDGE INSTANTLY, because an instant
// judgement always answers `nothing`. A haste effect, a rank the DBC does not
// carry and a core that resolved nothing all look like zero from here.
void AZeroCastTimeCannotProduceAWindowThatJudgesInstantly()
{
    CheckNum("zero is floored", CastVerifyWindowMs(0, 0, 8000, 40000), 8000);
    CheckNum("a short cast is floored", CastVerifyWindowMs(500, 1000, 8000, 40000), 8000);
}

// THE CEILING IS THE HOLD. A window longer than the hold that covers it ends
// with the expiry sweep handing the character back mid-cast.
void TheCeilingCapsTheWindow()
{
    CheckNum("a long cast is capped", CastVerifyWindowMs(60000, 5000, 8000, 40000), 40000);
}

// ...AND THE CEILING WINS OVER THE FLOOR, because of which of the two bugs is
// worse. A caller that passes a nonsensical pair gets the ceiling.
void TheCeilingWinsOverTheFloor()
{
    CheckNum("ceiling beats floor", CastVerifyWindowMs(0, 0, 30000, 9000), 9000);
}

void NonsenseInputsSaturateRatherThanWrap()
{
    CheckNum("an overflowing sum saturates into the ceiling",
             CastVerifyWindowMs(0xFFFFFFFFu, 5000, 8000, 40000), 40000);
}

// ----------------------------------------------------------- the retries --

// WHAT A SENDER SHOULD DO NEXT, WHICH IS THE HALF A BARE REFUSAL NEVER SAID.

void TheSpellAndTheSpellbookAreNeverWorthAskingAgain()
{
    CheckRetry("an unknown spell", CastRefusal::UnknownSpell, TownRetry::Never);
    CheckRetry("a spell not learned", CastRefusal::NotLearned, TownRetry::Never);
    CheckRetry("a passive spell", CastRefusal::Passive, TownRetry::Never);
    CheckRetry("no bot AI", CastRefusal::NoBotAI, TownRetry::Never);
}

void AMalformedRowIsNeverWorthAskingAgainEither()
{
    CheckRetry("no spell id", CastRefusal::NoSpellId, TownRetry::Never);
    CheckRetry("a name instead of an id", CastRefusal::NotANumber, TownRetry::Never);
    CheckRetry("an absurd id", CastRefusal::SpellIdTooBig, TownRetry::Never);
    CheckRetry("a bad grammar", CastRefusal::BadTarget, TownRetry::Never);
    CheckRetry("a bad target name", CastRefusal::BadTargetName, TownRetry::Never);
}

// EVERYTHING THE CHARACTER IS DOING ENDS ON ITS OWN, so the same row here in a
// moment is the right answer.
void EverythingTheCharacterIsDoingEndsOnItsOwn()
{
    CheckRetry("moving", CastRefusal::Moving, TownRetry::Later);
    CheckRetry("in combat", CastRefusal::InCombat, TownRetry::Later);
    CheckRetry("dead", CastRefusal::Dead, TownRetry::Later);
    CheckRetry("in flight", CastRefusal::InFlight, TownRetry::Later);
    CheckRetry("stunned", CastRefusal::Stunned, TownRetry::Later);
    CheckRetry("already casting", CastRefusal::AlreadyCasting, TownRetry::Later);
    CheckRetry("on cooldown", CastRefusal::OnCooldown, TownRetry::Later);
    CheckRetry("on the global cooldown", CastRefusal::OnGlobalCooldown, TownRetry::Later);
    CheckRetry("out of mana", CastRefusal::NotEnoughPower, TownRetry::Later);
    CheckRetry("another row is running", CastRefusal::AlreadyRunning, TownRetry::Later);
}

// BEING SOMEWHERE ELSE IS WHAT ANSWERS THESE. The reagent belongs here and not
// under `later`: nothing about standing still produces a Rune of Portals, and
// the thing that does is a trip to a vendor.
void WalkingSomewhereElseFixesTheseAndOnlyThese()
{
    CheckRetry("out of range", CastRefusal::TargetOutOfRange, TownRetry::Elsewhere);
    CheckRetry("the target is on another map", CastRefusal::TargetOtherMap,
               TownRetry::Elsewhere);
    CheckRetry("a missing reagent", CastRefusal::MissingReagent, TownRetry::Elsewhere);
}

void AnUnknownRefusalIsWorthOneMoreTry()
{
    CheckRetry("a refusal this table has never heard of",
               "some wall nobody has written down yet", TownRetry::Later);
    CheckRetry("an empty refusal", "", TownRetry::Later);
}

// NO TWO REFUSALS MAY READ THE SAME, because a table keyed on the literal
// cannot tell two identical sentences apart, and a row that names the wrong
// wall is the thing that costs an afternoon.
void EveryRefusalLiteralIsDistinctAndCarriesNoQuote()
{
    char const* const all[] = {
        CastRefusal::NoSession,       CastRefusal::NotInWorld,
        CastRefusal::NoBotAI,         CastRefusal::NotOwnMover,
        CastRefusal::Dead,            CastRefusal::InFlight,
        CastRefusal::InCombat,        CastRefusal::Stunned,
        CastRefusal::LoggingOut,      CastRefusal::Trading,
        CastRefusal::OnTransport,     CastRefusal::AlreadyCasting,
        CastRefusal::Moving,          CastRefusal::UnknownSpell,
        CastRefusal::NotLearned,      CastRefusal::Passive,
        CastRefusal::OnCooldown,      CastRefusal::OnGlobalCooldown,
        CastRefusal::NotEnoughPower,  CastRefusal::MissingReagent,
        CastRefusal::NoSuchTarget,    CastRefusal::TargetOtherMap,
        CastRefusal::TargetDead,      CastRefusal::TargetOutOfRange,
        CastRefusal::AlreadyRunning,  CastRefusal::NoSpellId,
        CastRefusal::NotANumber,      CastRefusal::SpellIdTooBig,
        CastRefusal::BadTarget,       CastRefusal::BadTargetName,
    };
    std::size_t const count = sizeof(all) / sizeof(all[0]);
    for (std::size_t i = 0; i < count; ++i)
    {
        std::string const one(all[i]);
        if (one.empty())
        {
            std::printf("FAIL an empty refusal literal at index %u\n", unsigned(i));
            ++failures;
        }
        // The literals go straight into an UPDATE. A quote in one of them is a
        // broken statement, and every executor in this module keeps the rule.
        if (one.find('\'') != std::string::npos || one.find('"') != std::string::npos)
        {
            std::printf("FAIL a refusal literal carries a quote: %s\n", all[i]);
            ++failures;
        }
        for (std::size_t j = i + 1; j < count; ++j)
        {
            if (one == all[j])
            {
                std::printf("FAIL two refusal literals read the same: %s\n", all[i]);
                ++failures;
            }
        }
    }
}

// ...AND THEY MUST NOT COLLIDE WITH THE POST-CAST SENTENCES EITHER, which are a
// different measurement of a different moment. `character is moving` sent
// nothing; `the character was moving when the cast was driven` sent the packet.
void ThePreflightRefusalsAreDistinctFromThePostCastWalls()
{
    namespace CastWall = OverseerDecisions::CastWall;
    Check("moving reads differently before and after",
          std::string(CastRefusal::Moving) != std::string(CastWall::Moving), true);
    Check("in flight reads differently before and after",
          std::string(CastRefusal::InFlight) != std::string(CastWall::InFlight), true);
    Check("on cooldown reads differently before and after",
          std::string(CastRefusal::OnCooldown) != std::string(CastWall::OnCooldown), true);
    Check("already casting reads differently before and after",
          std::string(CastRefusal::AlreadyCasting) != std::string(CastWall::AlreadyCasting),
          true);
}

}  // namespace

int main()
{
    ABareSpellIdIsACastOnSelf();
    WhitespaceAroundTheRowIsNotAnError();
    OnSelfIsTheBareFormSaidOutLoud();
    ANamedTargetIsCarriedThrough();
    ASpellNameIsRefusedWithASentence();
    AnEmptyRowIsRefusedRatherThanGuessedAt();
    SpellIdZeroIsNotSilentlyAccepted();
    AnAbsurdSpellIdIsRefusedRatherThanWrapped();
    ATrailingWordThatIsNotATargetIsRefused();
    ATargetThatCannotBeACharacterNameIsRefusedAsOne();

    AnObjectThatIsStandingThereIsMade();
    AnObjectThatIsNotThereIsNothing();
    TheObjectBeatsTheCostThatWasAlsoPaid();
    ASelfTeleportIsJudgedOnWhereTheCharacterIs();
    TheObjectBeatsTheTeleport();
    ASpellWithNoObjectAndNoTeleportIsJudgedOnWhatItCost();
    ASpellThatLeavesNoTraceIsUnreadableAndNotAFailure();
    ACasterThatCannotBeReadIsUnreadable();
    TheOutcomeWordsAreTheWordsARowCarries();

    ADifferentMapIsAMoveWhateverTheCoordinatesSay();
    APaceOfDriftIsNotATeleport();
    HeightCountsTowardTheDistance();
    AnUnreadPlaceIsNeverAMove();

    TheWindowIsTheCastPlusTheMargin();
    AZeroCastTimeCannotProduceAWindowThatJudgesInstantly();
    TheCeilingCapsTheWindow();
    TheCeilingWinsOverTheFloor();
    NonsenseInputsSaturateRatherThanWrap();

    TheSpellAndTheSpellbookAreNeverWorthAskingAgain();
    AMalformedRowIsNeverWorthAskingAgainEither();
    EverythingTheCharacterIsDoingEndsOnItsOwn();
    WalkingSomewhereElseFixesTheseAndOnlyThese();
    AnUnknownRefusalIsWorthOneMoreTry();
    EveryRefusalLiteralIsDistinctAndCarriesNoQuote();
    ThePreflightRefusalsAreDistinctFromThePostCastWalls();

    if (failures)
    {
        std::printf("%d cast check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: the cast decisions hold\n");
    return EXIT_SUCCESS;
}
