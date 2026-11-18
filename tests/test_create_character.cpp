/*
 * The create-character verb (2026-11-17): the row's grammar, every gate in
 * order, the idempotent answer, the faction rule, the level rule, and the
 * choice of the account that takes the new character.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace OverseerDecisions;

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

bool Says(CreateCharacterVerdict const& v, char const* text)
{
    return v.route == CreateRoute::Refuse && std::strcmp(v.reason, text) == 0;
}

CreateCharacterFacts Ready()
{
    CreateCharacterFacts f;
    f.switchOn = true;
    f.maxLevel = 1;
    f.nameValid = true;
    f.raceClassLegal = true;
    f.raceEnabled = true;
    f.freeAccount = 7;
    return f;
}

CreateCharacterRequest Ask(char const* command)
{
    return ParseCreateCharacterRequest(command);
}

void TheGrammar()
{
    Check("the verb is recognised", IsCreateCharacterRow("create-character human warrior male alliance"));
    Check("any other first word is not", !IsCreateCharacterRow("rename-to Mug"));
    Check("an empty command is not", !IsCreateCharacterRow(""));

    CreateCharacterRequest r = Ask("create-character human warrior male alliance");
    Check("a plain row parses", r.ok && r.race == 1 && r.cls == 1 && r.gender == 0 && r.alliance && r.level == 1);

    r = Ask("CREATE-CHARACTER Troll MAGE Female Horde");
    Check("case does not matter", r.ok && r.race == 8 && r.cls == 8 && r.gender == 1 && !r.alliance);

    r = Ask("create-character orc warrior male horde level 60");
    Check("an explicit level parses", r.ok && r.level == 60);

    Check("level 0 is refused", !Ask("create-character orc warrior male horde level 0").ok);
    Check("level 61 is refused", !Ask("create-character orc warrior male horde level 61").ok);
    Check("a word where the level should be is refused", !Ask("create-character orc warrior male horde level high").ok);
    Check("the word level without a number is refused", !Ask("create-character orc warrior male horde level").ok);
    Check("a misspelt level keyword is refused", !Ask("create-character orc warrior male horde lvl 60").ok);
    Check("a missing faction is refused", !Ask("create-character orc warrior male").ok);
    Check("an unknown race is refused", Ask("create-character worgen warrior male horde").error == std::string("unknown race"));
    Check("an unknown class is refused", Ask("create-character orc jester male horde").error == std::string("unknown class"));
    Check("an unknown gender is refused", !Ask("create-character orc warrior other horde").ok);
    Check("an unknown faction is refused", !Ask("create-character orc warrior male neutral").ok);
    Check("a death knight is refused as a grant",
          Ask("create-character human deathknight male alliance").error ==
              std::string("a death knight starts at level 55, which is a grant"));
}

void TheRaces()
{
    for (std::uint8_t race : {1, 3, 4, 7, 11})
        Check("alliance race", RaceIsAlliance(race));
    for (std::uint8_t race : {2, 5, 6, 8, 10})
        Check("horde race", !RaceIsAlliance(race));
}

void TheGates()
{
    CreateCharacterRequest const req = Ask("create-character human warrior male alliance");
    CreateCharacterFacts f = Ready();
    Check("everything fine: create", CreateCharacterVerdictFor(req, f).route == CreateRoute::Create);

    f = Ready();
    f.switchOn = false;
    Check("switch off", Says(CreateCharacterVerdictFor(req, f),
                             "character creation is off (Overseer.CreateCharacter.Enable)"));

    Check("a bad request says its own error",
          Says(CreateCharacterVerdictFor(Ask("create-character human warrior male"), Ready()),
               "malformed create-character: want race, class, gender, faction and an optional level N"));

    f = Ready();
    f.nameValid = false;
    Check("an invalid name", Says(CreateCharacterVerdictFor(req, f), "the core refuses that name"));

    f = Ready();
    f.nameTaken = true;
    Check("a name taken by another character",
          Says(CreateCharacterVerdictFor(req, f), "the name is taken by a different character"));

    f = Ready();
    f.nameTaken = true;
    f.takenIsSame = true;
    CreateCharacterVerdict v = CreateCharacterVerdictFor(req, f);
    Check("a name already made as asked is a no-op", v.route == CreateRoute::AlreadyExists);

    // Re-running a finished row must read `exists` even if the facts that only
    // matter to a new creation (a free slot, the faction) have since changed.
    f.freeAccount = 0;
    Check("exists does not need a free slot",
          CreateCharacterVerdictFor(req, f).route == CreateRoute::AlreadyExists);
    f.switchOn = true;
    Check("exists ignores the level gate",
          CreateCharacterVerdictFor(Ask("create-character human warrior male alliance level 60"), f).route ==
              CreateRoute::AlreadyExists);

    f = Ready();
    f.rowInFlight = true;
    Check("a creation in flight", CreateCharacterVerdictFor(req, f).route == CreateRoute::Refuse);

    Check("a horde race for an alliance guild",
          Says(CreateCharacterVerdictFor(Ask("create-character orc warrior male alliance"), Ready()),
               "the race is not of the guild's faction"));
    Check("an alliance race for a horde guild",
          Says(CreateCharacterVerdictFor(Ask("create-character human warrior male horde"), Ready()),
               "the race is not of the guild's faction"));

    f = Ready();
    f.raceEnabled = false;
    Check("a race the realm disabled", Says(CreateCharacterVerdictFor(req, f),
                                            "the realm has that race disabled for creation"));

    f = Ready();
    f.raceClassLegal = false;
    Check("an illegal pair", Says(CreateCharacterVerdictFor(req, f), "that race and class cannot be combined"));

    f = Ready();
    f.freeAccount = 0;
    Check("no free slot", Says(CreateCharacterVerdictFor(req, f),
                               "no listed bot account has a free character slot"));
}

void TheLevel()
{
    CreateCharacterRequest const sixty = Ask("create-character orc warrior male horde level 60");
    CreateCharacterFacts f = Ready();
    Check("default max level 1 refuses a level 60 row",
          Says(CreateCharacterVerdictFor(sixty, f), "level above Overseer.CreateCharacter.MaxLevel"));

    f.maxLevel = 60;
    Check("max level 60 allows it", CreateCharacterVerdictFor(sixty, f).route == CreateRoute::Create);

    f.maxLevel = 40;
    Check("max level 40 refuses 60", CreateCharacterVerdictFor(sixty, f).route == CreateRoute::Refuse);
    Check("max level 40 allows 40",
          CreateCharacterVerdictFor(Ask("create-character orc warrior male horde level 40"), f).route ==
              CreateRoute::Create);

    f = Ready();
    Check("a level 1 row needs no allowance",
          CreateCharacterVerdictFor(Ask("create-character orc warrior male horde level 1"), f).route ==
              CreateRoute::Create);
}

void TheAccountChoice()
{
    std::vector<CreationAccount> accounts = {{10, 4}, {11, 2}, {12, 2}, {13, 9}};
    Check("fewest characters, lowest id on a tie", PickCreationAccount(accounts, 10) == 11);
    Check("a full account is skipped", PickCreationAccount({{10, 10}, {11, 10}}, 10) == 0);
    Check("under the cap is enough", PickCreationAccount({{10, 9}}, 10) == 10);
    Check("an empty list is none", PickCreationAccount({}, 10) == 0);
    Check("id 0 is never chosen", PickCreationAccount({{0, 0}}, 10) == 0);
}


void TheAccountVerb()
{
    Check("create-account is recognised", IsCreateAccountRow("create-account"));
    Check("another word is not", !IsCreateAccountRow("create-character human warrior male alliance"));
    Check("the command is the one word", CreateAccountCommandOk("create-account") && !CreateAccountCommandOk("create-account now") && !CreateAccountCommandOk("create-accounts"));
    Check("the account password fits the core's 16 character limit", CREATE_ACCOUNT_PASSWORD_LENGTH > 0 && CREATE_ACCOUNT_PASSWORD_LENGTH <= 16);
    Check("a good name", AccountNameValid("JUG") && AccountNameValid("TUG2"));
    Check("one letter is short", !AccountNameValid("J"));
    Check("eighteen is long", !AccountNameValid("ABCDEFGHIJKLMNOPQR"));
    Check("punctuation is refused", !AccountNameValid("JU G") && !AccountNameValid("JU;G"));

    CreateAccountFacts f;
    f.switchOn = true;
    f.commandOk = true;
    f.nameValid = true;
    f.listed = true;
    Check("a listed new account is created", CreateAccountVerdictFor(f).route == CreateRoute::Create);

    CreateAccountFacts off = f;
    off.switchOn = false;
    Check("switch off", CreateAccountVerdictFor(off).route == CreateRoute::Refuse);

    CreateAccountFacts bad = f;
    bad.commandOk = false;
    Check("a malformed command", CreateAccountVerdictFor(bad).route == CreateRoute::Refuse);

    bad = f;
    bad.nameValid = false;
    Check("a bad name", CreateAccountVerdictFor(bad).route == CreateRoute::Refuse);

    bad = f;
    bad.randomBotName = true;
    Check("a random bot account name", CreateAccountVerdictFor(bad).route == CreateRoute::Refuse);

    bad = f;
    bad.listed = false;
    Check("an unlisted account is never invented", CreateAccountVerdictFor(bad).route == CreateRoute::Refuse);

    CreateAccountFacts again = f;
    again.exists = true;
    Check("an existing account is a no-op", CreateAccountVerdictFor(again).route == CreateRoute::AlreadyExists);
    again.listed = false;
    Check("an existing account that was delisted still reads refused",
          CreateAccountVerdictFor(again).route == CreateRoute::Refuse);
}

// Every end game family has a tank and a healer, and every name's race is of
// its guild's faction. The rosters are the ones the tracking issue lists.
struct Seat
{
    char const* name;
    char const* race;
    char const* cls;
};

void TheFamiliesFit()
{
    struct Family
    {
        char const* guild;
        bool alliance;
        std::vector<Seat> seats;
    };
    std::vector<Family> const families = {
        {"Rumble", false,
         {{"Jug", "orc", "warrior"}, {"Jrog", "orc", "shaman"}, {"Ujja", "troll", "priest"},
          {"Oj", "troll", "mage"}, {"Jork", "tauren", "druid"}}},
        {"Clobber", false,
         {{"Sug", "orc", "warrior"}, {"Srog", "orc", "shaman"}, {"Ussa", "troll", "priest"},
          {"Os", "troll", "mage"}, {"Sork", "tauren", "druid"}}},
        {"Hollow", true,
         {{"Tug", "human", "warrior"}, {"Trog", "dwarf", "paladin"}, {"Utta", "human", "priest"},
          {"Ot", "human", "mage"}, {"Tork", "gnome", "rogue"}}},
        {"Thicket", true,
         {{"Pug", "human", "warrior"}, {"Prog", "dwarf", "paladin"}, {"Uppa", "human", "priest"},
          {"Op", "human", "mage"}, {"Pork", "gnome", "rogue"}}},
    };
    for (Family const& family : families)
    {
        bool tank = false;
        bool healer = false;
        for (Seat const& seat : family.seats)
        {
            std::string const command = std::string("create-character ") + seat.race + " " + seat.cls +
                                        " male " + (family.alliance ? "alliance" : "horde") + " level 60";
            CreateCharacterRequest const r = ParseCreateCharacterRequest(command);
            CreateCharacterFacts f = Ready();
            f.maxLevel = 60;
            Check(seat.name, r.ok && CreateCharacterVerdictFor(r, f).route == CreateRoute::Create);
            if (!std::strcmp(seat.cls, "warrior"))
                tank = true;
            // A paladin or a priest heals; the Horde family heals with a
            // priest and a shaman, the Alliance family with a priest and a
            // paladin.
            if (!std::strcmp(seat.cls, "priest"))
                healer = true;
        }
        Check(family.guild, tank && healer);
    }
}

}  // namespace

int main()
{
    TheGrammar();
    TheRaces();
    TheGates();
    TheLevel();
    TheAccountChoice();
    TheAccountVerb();
    TheFamiliesFit();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
