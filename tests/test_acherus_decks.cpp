// The two decks of Acherus (map 609): a walk across them steps on the pad that
// joins them, and a death knight a class row leaves on a deck settles there.
// Every coordinate below is the dev realm's own world data, read on 2026-10-09:
// the two pad creatures (spawns 128753 and 128754), the teleports'
// spell_target_position rows (54699 and 54725), and the spawns and character
// positions named beside each case.
#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void expect(bool ok, char const* what)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

MailWalkPoint at(float x, float y, float z)
{
    MailWalkPoint p;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

std::vector<TeleportPad> acherusPads()
{
    TeleportPad up;  // "Teleport - Hall -> Heart", spell 54699
    up.at = at(2389.99f, -5640.93f, 378.23f);
    up.radius = 3.0f;
    up.exit = at(2418.67f, -5621.41f, 420.644f);
    TeleportPad down;  // "Teleport - Heart -> Hall", spell 54725
    down.at = at(2383.65f, -5645.24f, 420.9f);
    down.radius = 3.0f;
    down.exit = at(2402.15f, -5633.74f, 377.021f);
    return {up, down};
}

bool near(float got, float want)
{
    return std::fabs(got - want) < 0.5f;
}
}  // namespace

int main()
{
    std::vector<TeleportPad> const pads = acherusPads();
    MailWalkPoint const sword = at(2439.37f, -5672.16f, 420.643f);       // gameobject 65945
    MailWalkPoint const mograine = at(2460.5f, -5593.5f, 367.5f);        // creature 128470
    MailWalkPoint const lowerWest = at(2326.49f, -5651.24f, 382.24f);    // walk row 803442
    MailWalkPoint const underSword = at(2447.6f, -5666.1f, 376.9f);      // where it stood after
    MailWalkPoint const bindPoint = at(2358.17f, -5663.21f, 426.027f);   // hearth row 803258
    MailWalkPoint const ground = at(2156.39f, -5813.29f, 101.909f);      // ...and where it was
    MailWalkPoint const groundCamp = at(2316.6f, -5738.6f, 156.0f);      // creature 130344
    MailWalkPoint const balcony = at(2440.0f, -5600.0f, 444.4f);

    // ---- the walk across the decks ------------------------------------------
    TeleportPadChoice c = ChooseTeleportPad(pads, lowerWest, sword);
    expect(c.index == 0, "the Hall of Command walks to the pad up to the Heart for a sword chest");
    expect(near(c.viaYards, 64.4f + 54.8f), "the way by the pad is walker-to-pad plus exit-to-chest");

    c = ChooseTeleportPad(pads, underSword, sword);
    expect(c.index == 0, "standing 44 yards under the chest still takes the pad");
    expect(WalkYardsToGo(pads, underSword, sword) > 100.f,
           "under the chest the way left is the way round by the pad, not the 44 yards up");

    c = ChooseTeleportPad(pads, bindPoint, sword);
    expect(c.index < 0, "a walk on the Heart to a chest on the Heart takes no pad");
    expect(near(WalkYardsToGo(pads, bindPoint, sword), 81.9f),
           "with no pad the way left is the straight line");

    c = ChooseTeleportPad(pads, bindPoint, mograine);
    expect(c.index == 1, "the Heart walks to the pad down to the Hall for a giver below");

    expect(ChooseTeleportPad(pads, ground, sword).index < 0,
           "the ground below Acherus has no pad at its level: no pad is walked to");
    expect(ChooseTeleportPad(pads, lowerWest, groundCamp).index < 0,
           "no pad sets a walker down on the ground");
    expect(ChooseTeleportPad(pads, at(3400.f, -5640.f, 380.f), sword).index < 0,
           "a pad a thousand yards off is no step in a walk");
    expect(ChooseTeleportPad({}, lowerWest, sword).index < 0, "a map with no pads walks straight");

    // The shorter way round wins when two pads serve.
    std::vector<TeleportPad> two = pads;
    TeleportPad farUp = pads[0];
    farUp.at = at(2250.f, -5550.f, 378.f);
    two.insert(two.begin(), farUp);
    c = ChooseTeleportPad(two, lowerWest, sword);
    expect(c.index == 1, "of two pads up, the shorter way round by one is taken");

    // A pad that leaves the walker on its own level joins nothing.
    std::vector<TeleportPad> flat = pads;
    flat[0].exit.z = flat[0].at.z;
    expect(ChooseTeleportPad(flat, lowerWest, sword).index < 0,
           "a pad that keeps the walker on its level is not a way up");

    // ---- where the settle applies ---------------------------------------------
    expect(OnAPaddedLevel(pads, bindPoint), "the bind point on the Heart is a padded level");
    expect(OnAPaddedLevel(pads, lowerWest), "the Hall of Command is a padded level");
    expect(OnAPaddedLevel(pads, balcony), "the Heart's balcony is on the upper deck");
    expect(!OnAPaddedLevel(pads, ground), "the ground of the zone is not a deck");
    expect(!OnAPaddedLevel(pads, groundCamp), "the ground under Acherus is not a deck");
    expect(!OnAPaddedLevel({}, bindPoint), "nothing is a deck on a map with no pads");

    DeckSettleFacts facts;
    facts.mapId = DEATH_KNIGHT_START_MAP_ID;
    facts.deathKnight = true;
    facts.onAPaddedLevel = true;
    facts.alive = true;
    expect(SettlesOnTheDeck(facts), "a death knight a row left on a deck settles there");
    DeckSettleFacts f = facts;
    f.mapId = 0;
    expect(!SettlesOnTheDeck(f), "outside the start zone nobody settles");
    f = facts;
    f.deathKnight = false;
    expect(!SettlesOnTheDeck(f), "only a death knight settles");
    f = facts;
    f.onAPaddedLevel = false;
    expect(!SettlesOnTheDeck(f), "on the ground a hunt's grind is the work: no settle");
    f = facts;
    f.alive = false;
    expect(!SettlesOnTheDeck(f), "a dead character is not held");
    f = facts;
    f.inCombat = true;
    expect(!SettlesOnTheDeck(f), "a character in a fight is not held");
    f = facts;
    f.inFlight = true;
    expect(!SettlesOnTheDeck(f), "a character on a taxi is not held");
    f = facts;
    f.heldByAnother = true;
    expect(!SettlesOnTheDeck(f), "another verb's hold is left alone");

    // ---- what lifts it, and what takes it over ----------------------------------
    expect(RowLiftsDeckSettle("quest"), "the next class row lifts the settle");
    expect(RowLiftsDeckSettle("job"), "a walk row lifts the settle");
    expect(RowLiftsDeckSettle("hearth"), "a hearth row lifts the settle");
    expect(!RowLiftsDeckSettle("probe"), "a probe only reads");
    expect(!RowLiftsDeckSettle("chat"), "a chat line only speaks");
    expect(HoldYieldsToWalk(DECK_SETTLE_HOLD_VERB), "a walk takes over the settle");
    expect(!HoldYieldsToWalk("hearth"), "a walk does not take over a hearth's hold");
    expect(!HoldYieldsToWalk("stage"), "a walk does not take over a staging hold");
    expect(!HoldYieldsToWalk("questuse"), "a walk does not take over a quest use's hold");

    if (failures)
        std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
