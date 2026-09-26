/*
 * kind='naturalize': what a reset and a strip keep, what they remove, and
 * when they refuse.
 *
 * THE CASES ARE THE DEV REALM'S, measured read-only on 2026-09-24. 132 guild
 * bots of two guilds, one of them a level 55 death knight. A family whose
 * warrior holds all sixteen warrior weapon skills at 300/300 because the bot
 * factory set them to level x 5, and whose three characters wear six
 * Netherweave Bags a GM issued to one of them, which he then handed on two at a
 * time. And a Mining Pick on the family's head that no GM command ever issued,
 * which must be left alone even though another member's pick was.
 *
 * And, from the operator's second decision, the family taken down to the
 * level its experience would have bought at the normal rate: the realm has run
 * ten times the experience since 2026-09-10 00:53 UTC, so a tenth of what each
 * character earned since then, added to where it stood, is where it would be.
 *
 * WHAT THESE PROTECT. The verbs are irreversible, so the tests pin the lines
 * that decide what goes: the guild membership and rank a reset keeps, the
 * `randomize` value it must never clear, the profession and talent spells a
 * strip keeps, and the GM attribution that refuses to guess.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using OverseerDecisions::BoostLevelAchievements;
using OverseerDecisions::BotValueAction;
using OverseerDecisions::BotValueStep;
using OverseerDecisions::CompletedAchievement;
using OverseerDecisions::GmAttribution;
using OverseerDecisions::GmHolding;
using OverseerDecisions::GmIssue;
using OverseerDecisions::GmIssuedInstancesOf;
using OverseerDecisions::IsLevelAchievement;
using OverseerDecisions::DuesAmountFromSource;
using OverseerDecisions::DuesDiscard;
using OverseerDecisions::DuesDiscardFor;
using OverseerDecisions::DuesLetter;
using OverseerDecisions::ExperienceBetween;
using OverseerDecisions::NATURALIZE_PART_GOLD;
using OverseerDecisions::NATURALIZE_DEATH_KNIGHT_RESET_LEVEL;
using OverseerDecisions::NATURALIZE_RESET_LEVEL;
using OverseerDecisions::IsResetMode;
using OverseerDecisions::ResetStart;
using OverseerDecisions::ResetStartFor;
using OverseerDecisions::LoweredSkillValue;
using OverseerDecisions::LowerSpellDecisionFor;
using OverseerDecisions::LowerSpellFacts;
using OverseerDecisions::MailsNeededFor;
using OverseerDecisions::NATURALIZE_PART_LOWER;
using OverseerDecisions::NaturalLevel;
using OverseerDecisions::NaturalLevelFor;
using OverseerDecisions::NameListHas;
using OverseerDecisions::NATURALIZE_PART_ITEMS;
using OverseerDecisions::NATURALIZE_PART_RESET;
using OverseerDecisions::NATURALIZE_PART_RIDING;
using OverseerDecisions::NATURALIZE_PART_SPELLS;
using OverseerDecisions::NATURALIZE_PART_WEAPONS;
using OverseerDecisions::NATURALIZE_STRIP_PARTS;
using OverseerDecisions::NaturalizeFacts;
using OverseerDecisions::NaturalizeMode;
using OverseerDecisions::NaturalizeModeWord;
using OverseerDecisions::NaturalizePartWord;
using OverseerDecisions::NaturalizeRefusal;
using OverseerDecisions::NaturalizeRefusalSaid;
using OverseerDecisions::NaturalizeRequest;
using OverseerDecisions::NaturalizeVerdictFor;
using OverseerDecisions::ParseGmAdditem;
using OverseerDecisions::ParseNameList;
using OverseerDecisions::ParseNaturalizeRequest;
using OverseerDecisions::RESET_PARTS;
using OverseerDecisions::ResetPart;
using OverseerDecisions::ResetPartWord;
using OverseerDecisions::ResetRandomBotValues;
using OverseerDecisions::ResetTreatment;
using OverseerDecisions::ResetTreatmentFor;
using OverseerDecisions::ResetTreatmentWord;
using OverseerDecisions::StripSpellDecisionFor;
using OverseerDecisions::StripSpellFacts;
using OverseerDecisions::StripVerdict;
using OverseerDecisions::StripWeaponSkillDecisionFor;
using OverseerDecisions::StripWeaponSkillFacts;

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

// ------------------------------------------------------------------ grammar

void TestParse()
{
    NaturalizeRequest r = ParseNaturalizeRequest("reset-level-1");
    Check("reset parses", r.ok && r.mode == NaturalizeMode::ResetLevelOne && !r.dryRun);
    Check("a reset is one part", r.parts == NATURALIZE_PART_RESET);

    r = ParseNaturalizeRequest("reset-level-1 dry-run");
    Check("reset dry-run parses", r.ok && r.dryRun);

    r = ParseNaturalizeRequest("reset-level-55 dry-run");
    Check("the death knight reset parses",
          r.ok && r.mode == NaturalizeMode::ResetDeathKnight && r.dryRun && r.parts == NATURALIZE_PART_RESET);
    Check("its word is the level it goes to",
          std::string(NaturalizeModeWord(NaturalizeMode::ResetDeathKnight)) == "reset-level-55");
    Check("both resets are resets", IsResetMode(NaturalizeMode::ResetLevelOne) &&
                                        IsResetMode(NaturalizeMode::ResetDeathKnight) &&
                                        !IsResetMode(NaturalizeMode::StripFamilyGrants) &&
                                        !IsResetMode(NaturalizeMode::LowerToNaturalLevel));
    Check("the 55 reset takes no parts: or level:", !ParseNaturalizeRequest("reset-level-55 parts:items").ok &&
                                                        !ParseNaturalizeRequest("reset-level-55 level:55").ok);

    r = ParseNaturalizeRequest("  STRIP-FAMILY-GRANTS   Dry-Run ");
    Check("case and spacing do not matter", r.ok && r.mode == NaturalizeMode::StripFamilyGrants && r.dryRun);
    Check("a strip without parts: is every part", r.parts == NATURALIZE_STRIP_PARTS);

    r = ParseNaturalizeRequest("strip-family-grants parts:items,riding");
    Check("parts: limits a strip",
          r.ok && r.parts == (NATURALIZE_PART_ITEMS | NATURALIZE_PART_RIDING) && !r.dryRun);

    r = ParseNaturalizeRequest("strip-family-grants dry-run parts:spells,weapons");
    Check("parts: with a dry run",
          r.ok && r.dryRun && r.parts == (NATURALIZE_PART_SPELLS | NATURALIZE_PART_WEAPONS));

    Check("an unknown part is refused", !ParseNaturalizeRequest("strip-family-grants parts:gold").ok);
    Check("a reset takes no parts:", !ParseNaturalizeRequest("reset-level-1 parts:items").ok);
    Check("an unknown mode is refused", !ParseNaturalizeRequest("reset-level-2").ok);
    Check("an empty row is refused", !ParseNaturalizeRequest("   ").ok);
    Check("a stray word is refused, not ignored", !ParseNaturalizeRequest("reset-level-1 now").ok);
    Check("dry-run twice is refused", !ParseNaturalizeRequest("reset-level-1 dry-run dry-run").ok);
    Check("every refusal says why", std::strlen(ParseNaturalizeRequest("nope").error) > 0);

    r = ParseNaturalizeRequest("lower-to-natural-level level:38 dry-run");
    Check("lower parses with its level", r.ok && r.mode == NaturalizeMode::LowerToNaturalLevel && r.dryRun &&
                                            r.targetLevel == 38 && r.parts == NATURALIZE_PART_LOWER);
    Check("lower needs a level", !ParseNaturalizeRequest("lower-to-natural-level").ok);
    Check("level: must be a number", !ParseNaturalizeRequest("lower-to-natural-level level:abc").ok);
    Check("level: 0 is not a level", !ParseNaturalizeRequest("lower-to-natural-level level:0").ok);
    Check("level: twice is refused", !ParseNaturalizeRequest("lower-to-natural-level level:30 level:31").ok);
    Check("level: belongs to lower only", !ParseNaturalizeRequest("strip-family-grants level:30").ok &&
                                              !ParseNaturalizeRequest("reset-level-1 level:1").ok);
    Check("parts: does not apply to lower", !ParseNaturalizeRequest("lower-to-natural-level level:30 parts:items").ok);

    Check("mode words",
          std::string(NaturalizeModeWord(NaturalizeMode::ResetLevelOne)) == "reset-level-1" &&
              std::string(NaturalizeModeWord(NaturalizeMode::StripFamilyGrants)) == "strip-family-grants");
    Check("part words are the ledger's",
          std::string(NaturalizePartWord(NATURALIZE_PART_RESET)) == "reset" &&
              std::string(NaturalizePartWord(NATURALIZE_PART_ITEMS)) == "items" &&
              std::string(NaturalizePartWord(NATURALIZE_PART_RIDING)) == "riding" &&
              std::string(NaturalizePartWord(NATURALIZE_PART_WEAPONS)) == "weapons" &&
              std::string(NaturalizePartWord(NATURALIZE_PART_SPELLS)) == "spells" &&
              std::string(NaturalizePartWord(NATURALIZE_PART_LOWER)) == "lower");
}

void TestNameList()
{
    std::vector<std::string> const list = ParseNameList(" Cave , Bonkers,, ");
    Check("two names, ends trimmed, empties dropped", list.size() == 2 && list[0] == "Cave" && list[1] == "Bonkers");
    Check("a match ignores case", NameListHas(list, "cave") && NameListHas(list, "BONKERS"));
    Check("a guild not listed is not matched", !NameListHas(list, "Adventurer Union"));
    Check("no guild is never matched", !NameListHas(list, ""));
    Check("an empty list matches nothing", !NameListHas(ParseNameList(""), "Cave"));
}

// ------------------------------------------------------------------ gates

NaturalizeFacts GuildBot()
{
    NaturalizeFacts f;
    f.enabled = true;
    f.exists = true;
    f.guildListed = true;
    f.inFamily = false;
    f.playerbotsGateCovers = true;
    f.trainFactoryOn = false;
    return f;
}

NaturalizeFacts FamilyMember()
{
    NaturalizeFacts f = GuildBot();
    f.inFamily = true;
    return f;
}

void TestVerdict()
{
    NaturalizeRequest const reset = ParseNaturalizeRequest("reset-level-1");
    NaturalizeRequest const strip = ParseNaturalizeRequest("strip-family-grants");

    Check("a guild bot may be reset", NaturalizeVerdictFor(reset, GuildBot(), false) == NaturalizeRefusal::None);
    Check("a family member may be stripped",
          NaturalizeVerdictFor(strip, FamilyMember(), false) == NaturalizeRefusal::None);

    NaturalizeFacts f = GuildBot();
    f.enabled = false;
    Check("off by default refuses changes but permits a dry run",
          NaturalizeVerdictFor(reset, f, true) == NaturalizeRefusal::None);
    Check("off by default refuses an apply",
          NaturalizeVerdictFor(reset, f, false) == NaturalizeRefusal::Disabled);

    Check("a bad row is refused before anything is read",
          NaturalizeVerdictFor(ParseNaturalizeRequest("reset-level-9"), GuildBot(), true) ==
              NaturalizeRefusal::BadRequest);

    f = GuildBot();
    f.exists = false;
    Check("no such character", NaturalizeVerdictFor(reset, f, true) == NaturalizeRefusal::NoSuchCharacter);

    f = GuildBot();
    f.guildListed = false;
    Check("a bot outside the listed guilds is refused, dry run too",
          NaturalizeVerdictFor(reset, f, true) == NaturalizeRefusal::NotInNaturalGuild);

    Check("a family character is never reset, not even in a dry run",
          NaturalizeVerdictFor(reset, FamilyMember(), true) == NaturalizeRefusal::FamilyMember);
    Check("a guild bot is never stripped",
          NaturalizeVerdictFor(strip, GuildBot(), true) == NaturalizeRefusal::NotFamily);

    f = GuildBot();
    f.deathKnight = true;
    Check("the death knight is refused a level 1 reset",
          NaturalizeVerdictFor(reset, f, true) == NaturalizeRefusal::DeathKnight);
    Check("...and told the mode that fits it",
          std::strstr(NaturalizeRefusalSaid(NaturalizeRefusal::DeathKnight), "reset-level-55") != nullptr);

    // reset-level-55: the operator's 2026-09-26 decision for the guild's death
    // knight. The same gates as a level 1 reset, for a death knight only.
    NaturalizeRequest const dk = ParseNaturalizeRequest("reset-level-55");
    Check("the death knight may be reset to a fresh 55", NaturalizeVerdictFor(dk, f, false) == NaturalizeRefusal::None);
    Check("...and dry-run first", NaturalizeVerdictFor(dk, f, true) == NaturalizeRefusal::None);
    Check("a class that starts at 1 is refused a 55 reset, dry run too",
          NaturalizeVerdictFor(dk, GuildBot(), true) == NaturalizeRefusal::NotDeathKnight);
    Check("...and told the mode that fits it",
          std::strstr(NaturalizeRefusalSaid(NaturalizeRefusal::NotDeathKnight), "reset-level-1") != nullptr);
    NaturalizeFacts dkFamily = FamilyMember();
    dkFamily.deathKnight = true;
    Check("a family death knight is never reset",
          NaturalizeVerdictFor(dk, dkFamily, true) == NaturalizeRefusal::FamilyMember);
    NaturalizeFacts dkElsewhere = f;
    dkElsewhere.guildListed = false;
    Check("a death knight outside the listed guilds is refused",
          NaturalizeVerdictFor(dk, dkElsewhere, true) == NaturalizeRefusal::NotInNaturalGuild);
    NaturalizeFacts dkOff = f;
    dkOff.enabled = false;
    Check("Overseer.Natural.Enabled off refuses the 55 reset's apply",
          NaturalizeVerdictFor(dk, dkOff, false) == NaturalizeRefusal::Disabled);
    NaturalizeFacts dkGate = f;
    dkGate.playerbotsGateCovers = false;
    Check("the 55 reset waits for the playerbots gate too",
          NaturalizeVerdictFor(dk, dkGate, false) == NaturalizeRefusal::PlayerbotsGateOff);
    NaturalizeFacts dkCod = f;
    dkCod.codMail = 1;
    Check("COD mail stops the 55 reset", NaturalizeVerdictFor(dk, dkCod, false) == NaturalizeRefusal::CodMail);
    NaturalizeFacts dkAuction = f;
    dkAuction.openAuctions = 1;
    Check("an open auction stops the 55 reset",
          NaturalizeVerdictFor(dk, dkAuction, false) == NaturalizeRefusal::OpenAuction);
    NaturalizeFacts dkClient = f;
    dkClient.clientAttached = true;
    Check("the 55 reset never acts under a live client",
          NaturalizeVerdictFor(dk, dkClient, false) == NaturalizeRefusal::ClientAttached);
    NaturalizeFacts dkDone = f;
    dkDone.partsAlreadyDone = NATURALIZE_PART_RESET;
    Check("a death knight already reset is not reset again",
          NaturalizeVerdictFor(dk, dkDone, false) == NaturalizeRefusal::AlreadyDone);

    f = GuildBot();
    f.openAuctions = 1;
    Check("an open auction stops a reset", NaturalizeVerdictFor(reset, f, false) == NaturalizeRefusal::OpenAuction);
    Check("...but not its dry run", NaturalizeVerdictFor(reset, f, true) == NaturalizeRefusal::None);
    f = GuildBot();
    f.openBids = 2;
    Check("an open bid stops a reset", NaturalizeVerdictFor(reset, f, false) == NaturalizeRefusal::OpenAuction);
    f = GuildBot();
    f.codMail = 1;
    Check("COD mail stops a reset", NaturalizeVerdictFor(reset, f, false) == NaturalizeRefusal::CodMail);

    f = GuildBot();
    f.playerbotsGateCovers = false;
    Check("a reset waits for the playerbots gate",
          NaturalizeVerdictFor(reset, f, false) == NaturalizeRefusal::PlayerbotsGateOff);
    Check("...its dry run does not", NaturalizeVerdictFor(reset, f, true) == NaturalizeRefusal::None);
    Check("the playerbots gate is not a strip's business",
          NaturalizeVerdictFor(strip, [] {
              NaturalizeFacts x = FamilyMember();
              x.playerbotsGateCovers = false;
              return x;
          }(), false) == NaturalizeRefusal::None);

    f = FamilyMember();
    f.trainFactoryOn = true;
    Check("a strip waits for TrainRoster's factory to be off",
          NaturalizeVerdictFor(strip, f, false) == NaturalizeRefusal::TrainFactoryOn);
    Check("...its dry run does not", NaturalizeVerdictFor(strip, f, true) == NaturalizeRefusal::None);

    f = FamilyMember();
    f.clientAttached = true;
    Check("a real run never acts under a live client",
          NaturalizeVerdictFor(strip, f, false) == NaturalizeRefusal::ClientAttached);
    Check("a dry run reads a live client's character", NaturalizeVerdictFor(strip, f, true) == NaturalizeRefusal::None);

    f = GuildBot();
    f.partsAlreadyDone = NATURALIZE_PART_RESET;
    Check("a second reset is refused", NaturalizeVerdictFor(reset, f, false) == NaturalizeRefusal::AlreadyDone);
    Check("...and its dry run still reads", NaturalizeVerdictFor(reset, f, true) == NaturalizeRefusal::None);

    f = FamilyMember();
    f.partsAlreadyDone = NATURALIZE_PART_ITEMS | NATURALIZE_PART_RIDING;
    Check("a strip with parts left still runs", NaturalizeVerdictFor(strip, f, false) == NaturalizeRefusal::None);
    Check("a strip of only done parts is refused",
          NaturalizeVerdictFor(ParseNaturalizeRequest("strip-family-grants parts:riding"), f, false) ==
              NaturalizeRefusal::AlreadyDone);

    // lower-to-natural-level: family only, and only down.
    NaturalizeRequest const lower = ParseNaturalizeRequest("lower-to-natural-level level:38");
    f = FamilyMember();
    f.level = 60;
    f.trainFactoryOn = true;
    Check("the family may be lowered, whatever TrainRoster's factory says",
          NaturalizeVerdictFor(lower, f, false) == NaturalizeRefusal::None);
    NaturalizeFacts bot = GuildBot();
    bot.level = 60;
    Check("a guild bot is never lowered", NaturalizeVerdictFor(lower, bot, true) == NaturalizeRefusal::NotFamily);
    f.level = 38;
    Check("lowering to the level it has is refused, dry run too",
          NaturalizeVerdictFor(lower, f, true) == NaturalizeRefusal::LevelNotLower);
    f.level = 29;
    Check("raising is refused", NaturalizeVerdictFor(lower, f, true) == NaturalizeRefusal::LevelNotLower);
    f.level = 60;
    f.partsAlreadyDone = NATURALIZE_PART_LOWER;
    Check("a second lowering is refused", NaturalizeVerdictFor(lower, f, false) == NaturalizeRefusal::AlreadyDone);

    Check("every refusal has words", std::strlen(NaturalizeRefusalSaid(NaturalizeRefusal::DeathKnight)) > 0 &&
                                         std::strlen(NaturalizeRefusalSaid(NaturalizeRefusal::AlreadyDone)) > 0);
    Check("no refusal literal carries a quote",
          [] {
              for (int i = 0; i <= static_cast<int>(NaturalizeRefusal::AlreadyDone); ++i)
                  if (std::strchr(NaturalizeRefusalSaid(static_cast<NaturalizeRefusal>(i)), '\''))
                      return false;
              return true;
          }());
}

// ------------------------------------------------------------------ reset

void TestResetKeepsAndRemoves()
{
    Check("the character itself is kept", ResetTreatmentFor(ResetPart::Identity) == ResetTreatment::Keep);
    Check("guild membership is kept", ResetTreatmentFor(ResetPart::GuildMembership) == ResetTreatment::Keep);
    Check("guild rank is kept", ResetTreatmentFor(ResetPart::GuildRank) == ResetTreatment::Keep);
    Check("played time is kept", ResetTreatmentFor(ResetPart::PlayedTime) == ResetTreatment::Keep);

    Check("level goes back to the start", ResetTreatmentFor(ResetPart::Level) == ResetTreatment::RestoreToStart);
    Check("items go, and the starting outfit comes back",
          ResetTreatmentFor(ResetPart::Items) == ResetTreatment::RestoreToStart);
    Check("gold goes back to the starting money", ResetTreatmentFor(ResetPart::Money) == ResetTreatment::RestoreToStart);
    Check("skills go back to the defaults", ResetTreatmentFor(ResetPart::Skills) == ResetTreatment::RestoreToStart);
    Check("spells, mounts included, go back to the defaults",
          ResetTreatmentFor(ResetPart::Spells) == ResetTreatment::RestoreToStart);
    Check("reputation goes back to base", ResetTreatmentFor(ResetPart::Reputation) == ResetTreatment::RestoreToStart);
    Check("quests are cleared", ResetTreatmentFor(ResetPart::Quests) == ResetTreatment::RestoreToStart);
    Check("home and position are the race's start",
          ResetTreatmentFor(ResetPart::HomeAndPosition) == ResetTreatment::RestoreToStart);
    Check("inbox mail is removed", ResetTreatmentFor(ResetPart::InboxMail) == ResetTreatment::Remove);
    Check("pets are removed", ResetTreatmentFor(ResetPart::Pets) == ResetTreatment::Remove);

    int kept = 0;
    bool wordsOk = true;
    for (ResetPart p : RESET_PARTS)
    {
        if (ResetTreatmentFor(p) == ResetTreatment::Keep)
            ++kept;
        if (std::string(ResetPartWord(p)) == "unknown" ||
            std::string(ResetTreatmentWord(ResetTreatmentFor(p))) == "unknown")
            wordsOk = false;
    }
    Check("exactly four parts survive a reset: identity, guild, rank, played time", kept == 4);
    Check("a reset goes to level 1, not to a realm's configured start", NATURALIZE_RESET_LEVEL == 1);

    // What each reset puts back, as Player::Create gives a new character of
    // the class: a death knight at 55 with the heroic start gold, ten of its
    // starting food, and no talent points until its Ebon Hold chain pays them.
    ResetStart const one = ResetStartFor(NaturalizeMode::ResetLevelOne);
    Check("a level 1 reset starts at 1 with the normal start gold and four food",
          one.level == 1 && !one.heroicStartMoney && one.startFood == 4 && one.freeTalentPoints == 0);
    ResetStart const dk = ResetStartFor(NaturalizeMode::ResetDeathKnight);
    Check("a death knight reset starts at 55", dk.level == 55 && NATURALIZE_DEATH_KNIGHT_RESET_LEVEL == 55);
    Check("...with the heroic start gold", dk.heroicStartMoney);
    Check("...and ten of its starting food, as Player::Create gives a death knight", dk.startFood == 10);
    Check("...and no free talent points: a new one earns them in the Ebon Hold chain", dk.freeTalentPoints == 0);
    Check("a mode that is no reset has no start", ResetStartFor(NaturalizeMode::StripFamilyGrants).level == 0);
    Check("every part and treatment has a word for the result", wordsOk);
}

void TestResetBotValues()
{
    std::vector<BotValueAction> const steps = ResetRandomBotValues(NATURALIZE_RESET_LEVEL);
    auto find = [&](char const* event) -> BotValueAction const* {
        for (BotValueAction const& a : steps)
            if (std::string(a.event) == event)
                return &a;
        return nullptr;
    };

    BotValueAction const* randomize = find("randomize");
    Check("randomize is named", randomize != nullptr);
    Check("randomize is NEVER cleared: that is RandomizeFirst's full kit at level 1",
          randomize && randomize->step == BotValueStep::Leave);
    BotValueAction const* level = find("level");
    Check("level is set to 1", level && level->step == BotValueStep::Set && level->value == 1);
    Check("dead is cleared", find("dead") && find("dead")->step == BotValueStep::Clear);
    Check("revive is cleared", find("revive") && find("revive")->step == BotValueStep::Clear);
    Check("teleport is left to the patch", find("teleport") && find("teleport")->step == BotValueStep::Leave);
    Check("add is left: the guild keeps it online", find("add") && find("add")->step == BotValueStep::Leave);
    for (BotValueAction const& a : steps)
        Check("every value says why", std::strlen(a.why) > 0);

    bool dkLevel = false;
    for (BotValueAction const& a : ResetRandomBotValues(NATURALIZE_DEATH_KNIGHT_RESET_LEVEL))
        if (std::string(a.event) == "level")
            dkLevel = a.step == BotValueStep::Set && a.value == 55;
    Check("a death knight reset records level 55 for playerbots", dkLevel);
}

// ------------------------------------------------------------------ strip

void TestStripSpells()
{
    // Heroic Strike rank 2 on a level 60 warrior: sold by the warrior
    // trainer, taught by TrainRoster for nothing.
    StripSpellFacts trainerSpell;
    trainerSpell.classTrainer = true;
    Check("a class trainer spell nobody paid for goes",
          StripSpellDecisionFor(trainerSpell).verdict == StripVerdict::Remove);

    StripSpellFacts f = trainerSpell;
    f.trainerRecord = true;
    Check("...unless a purchase is recorded", StripSpellDecisionFor(f).verdict == StripVerdict::Keep);
    f = trainerSpell;
    f.talent = true;
    Check("a talent rank stays, even where a trainer sells the next rank",
          StripSpellDecisionFor(f).verdict == StripVerdict::Keep);
    f = trainerSpell;
    f.autoLearned = true;
    Check("a spell every character of the class starts with stays",
          StripSpellDecisionFor(f).verdict == StripVerdict::Keep);

    StripSpellFacts profession;
    profession.profession = true;
    profession.classTrainer = true;
    Check("a profession spell stays: professions stay", StripSpellDecisionFor(profession).verdict == StripVerdict::Keep);

    StripSpellFacts riding;
    riding.riding = true;
    Check("Expert Riding goes", StripSpellDecisionFor(riding).verdict == StripVerdict::Remove);
    riding.trainerRecord = true;
    Check("...unless it was bought", StripSpellDecisionFor(riding).verdict == StripVerdict::Keep);

    // Berserker Stance: only a quest teaches it.
    StripSpellFacts questOnly;
    questOnly.questTaught = true;
    Check("a quest spell without the quest goes", StripSpellDecisionFor(questOnly).verdict == StripVerdict::Remove);
    questOnly.questReward = true;
    Check("a quest spell with the quest done stays",
          StripSpellDecisionFor(questOnly).verdict == StripVerdict::Keep);

    StripSpellFacts dependent = trainerSpell;
    dependent.dependent = true;
    Check("a dependent spell is never judged on its own",
          StripSpellDecisionFor(dependent).verdict == StripVerdict::Keep);

    StripSpellFacts nothing;
    Check("a spell no trainer or quest teaches stays (an item, a racial, a mount)",
          StripSpellDecisionFor(nothing).verdict == StripVerdict::Keep);
    Check("every decision says why", std::strlen(StripSpellDecisionFor(nothing).why) > 0 &&
                                         std::strlen(StripSpellDecisionFor(trainerSpell).why) > 0);
}

void TestStripWeaponSkills()
{
    // Grug, level 60 warrior, every weapon skill 300/300.
    StripWeaponSkillFacts polearms;
    polearms.value = 300;
    Check("an untrained weapon skill goes", StripWeaponSkillDecisionFor(polearms).verdict == StripVerdict::Remove);
    polearms.trainerRecord = true;
    polearms.usedLevel = 0;
    Check("a trained one never wielded keeps a fresh value",
          StripWeaponSkillDecisionFor(polearms).verdict == StripVerdict::SetValue &&
              StripWeaponSkillDecisionFor(polearms).value == 1);

    StripWeaponSkillFacts axes;
    axes.startingSkill = true;
    axes.value = 300;
    axes.usedLevel = 60;
    Check("a starting weapon in hand at 60 keeps 300", StripWeaponSkillDecisionFor(axes).verdict == StripVerdict::Keep &&
                                                           StripWeaponSkillDecisionFor(axes).value == 300);

    StripWeaponSkillFacts swords;
    swords.startingSkill = true;
    swords.value = 300;
    swords.usedLevel = 34;
    Check("a starting weapon last wielded at 34 is capped at 170",
          StripWeaponSkillDecisionFor(swords).verdict == StripVerdict::SetValue &&
              StripWeaponSkillDecisionFor(swords).value == 170);

    swords.value = 120;
    Check("a value already under the cap is kept", StripWeaponSkillDecisionFor(swords).verdict == StripVerdict::Keep &&
                                                       StripWeaponSkillDecisionFor(swords).value == 120);

    StripWeaponSkillFacts unarmed;
    unarmed.startingSkill = true;
    unarmed.value = 300;
    Check("a starting skill never used goes to 1", StripWeaponSkillDecisionFor(unarmed).verdict == StripVerdict::SetValue &&
                                                       StripWeaponSkillDecisionFor(unarmed).value == 1);

    StripWeaponSkillFacts defense;
    defense.defense = true;
    defense.value = 300;
    Check("defense is kept", StripWeaponSkillDecisionFor(defense).verdict == StripVerdict::Keep &&
                                 StripWeaponSkillDecisionFor(defense).value == 300);

    // Idempotent: a second pass over the first pass's result changes nothing.
    swords.value = StripWeaponSkillDecisionFor([] {
                       StripWeaponSkillFacts s;
                       s.startingSkill = true;
                       s.value = 300;
                       s.usedLevel = 34;
                       return s;
                   }())
                       .value;
    Check("a capped skill is left alone the second time", StripWeaponSkillDecisionFor(swords).verdict == StripVerdict::Keep);
}

void TestGmAdditem()
{
    unsigned entry = 0;
    unsigned count = 0;
    Check("an issue parses", ParseGmAdditem(".additem 21841 2", entry, count) && entry == 21841 && count == 2);
    Check("a count defaults to one", ParseGmAdditem(".additem 12987", entry, count) && entry == 12987 && count == 1);
    Check("a negative count took items away", !ParseGmAdditem(".additem 730 -2", entry, count));
    Check("a zero count is nothing", !ParseGmAdditem(".additem 730 0", entry, count));
    Check("an item link is not an entry", !ParseGmAdditem(".additem |Hitem:21841|h 2", entry, count));
    Check("another command is not an issue", !ParseGmAdditem(".character level Og 40", entry, count));
    Check("a stray word is not an issue", !ParseGmAdditem(".additem 21841 2 now", entry, count));
}

void TestGmAttribution()
{
    // The six bags: all issued to Grug, two still on him, two each on Bork and
    // Og. The pick: issued to Grog, who holds it; Grug holds another.
    std::vector<GmIssue> const issues = {
        {"Grug", 21841, 2},
        {"Grug", 21841, 4},
        {"Grog", 2901, 1},
        {"Bork", 7005, 1},
    };
    std::vector<GmHolding> const holdings = {
        {"Grug", 21841, 1338543, false}, {"Grug", 21841, 1338660, false}, {"Og", 21841, 1338542, false},
        {"Og", 21841, 1338662, false},   {"Bork", 21841, 1338659, false}, {"Bork", 21841, 1338661, false},
        {"Grog", 2901, 1841109, false},  {"Grug", 2901, 1840706, false},  {"Bork", 7005, 1841108, false},
    };

    GmAttribution const grug = GmIssuedInstancesOf("Grug", issues, holdings);
    Check("Grug loses his two bags", grug.itemGuids == std::vector<unsigned>({1338543, 1338660}));
    Check("Grug's own pick has no GM record and is kept, with a note",
          grug.notes.size() == 1 && grug.notes[0].find("1840706") != std::string::npos);

    GmAttribution const og = GmIssuedInstancesOf("Og", issues, holdings);
    Check("Og loses the two he was handed", og.itemGuids == std::vector<unsigned>({1338542, 1338662}));
    GmAttribution const bork = GmIssuedInstancesOf("Bork", issues, holdings);
    Check("Bork loses his two bags and his knife",
          bork.itemGuids == std::vector<unsigned>({1338659, 1338661, 1841108}));
    GmAttribution const grog = GmIssuedInstancesOf("Grog", issues, holdings);
    Check("Grog loses his pick", grog.itemGuids == std::vector<unsigned>({1841109}));
    GmAttribution const ugga = GmIssuedInstancesOf("Ugga", issues, holdings);
    Check("Ugga holds nothing issued", ugga.itemGuids.empty() && ugga.notes.empty());

    // A looted instance is natural whoever holds it.
    std::vector<GmHolding> looted = holdings;
    looted[2].naturalRecord = true;   // one of Og's bags came from a loot event
    GmAttribution const ogLooted = GmIssuedInstancesOf("Og", issues, looted);
    Check("a looted instance is never attributed", ogLooted.itemGuids == std::vector<unsigned>({1338662}));

    // More candidates than the GM left unaccounted: nothing is guessed.
    std::vector<GmIssue> const one = {{"Grug", 21841, 3}};
    std::vector<GmHolding> const three = {
        {"Grug", 21841, 10, false}, {"Og", 21841, 11, false}, {"Bork", 21841, 12, false}, {"Ugga", 21841, 13, false}};
    GmAttribution const ogAmbiguous = GmIssuedInstancesOf("Og", one, three);
    Check("two unaccounted and three candidates: none attributed",
          ogAmbiguous.itemGuids.empty() && !ogAmbiguous.notes.empty());
    Check("...while the recipient's own is still his", GmIssuedInstancesOf("Grug", one, three).itemGuids ==
                                                           std::vector<unsigned>({10}));

    // A recipient holding more than it was issued keeps the newest.
    std::vector<GmHolding> const two = {{"Grog", 2901, 50, false}, {"Grog", 2901, 60, false}};
    GmAttribution const grogTwo = GmIssuedInstancesOf("Grog", {{"Grog", 2901, 1}}, two);
    Check("one issued, two held: the lower guid goes, the other is noted",
          grogTwo.itemGuids == std::vector<unsigned>({50}) && grogTwo.notes.size() == 1);
}

void TestBoostAchievements()
{
    Check("Level 10 to Level 80 are level achievements", IsLevelAchievement(6) && IsLevelAchievement(13) &&
                                                              !IsLevelAchievement(5) && !IsLevelAchievement(14));

    // Og: set to 40 at 02:58:55, +10 at 02:59:40 (completed 02:59:42), 60 at
    // 07:28:22 (completed 07:28:23). His Level 20 and 30 were earned days
    // earlier.
    std::int64_t const set40 = 1789009135;
    std::int64_t const up10 = 1789009180;
    std::int64_t const set60 = 1789025302;
    std::vector<CompletedAchievement> const og = {
        {7, 1788500000}, {8, 1788900000}, {9, set40}, {10, up10 + 2}, {11, set60 + 1}, {46, set40}};
    std::vector<unsigned> const boost = BoostLevelAchievements(og, {set40, up10, set60}, 5);
    Check("Level 40, 50 and 60 are the boost's", boost == std::vector<unsigned>({9, 10, 11}));
    Check("an achievement that is not a level one is never listed",
          BoostLevelAchievements({{46, set40}}, {set40}, 5).empty());
    Check("a level achievement minutes away is earned",
          BoostLevelAchievements({{9, set40 + 600}}, {set40}, 5).empty());
}

// player_xp_for_level on the dev realm, levels 1 to 60.
std::vector<std::uint64_t> XpTable()
{
    return {0,      400,    900,    1400,   2100,   2800,   3600,   4500,   5400,   6500,   7600,   8700,   9800,
            11000,  12300,  13600,  15000,  16400,  17800,  19300,  20800,  22400,  24000,  25500,  27200,  28900,
            30500,  32200,  33900,  36300,  38800,  41600,  44600,  48000,  51400,  55000,  58700,  62400,  66200,
            70200,  74300,  78500,  82800,  87100,  91600,  96300,  101000, 105800, 110700, 115700, 120900, 126100,
            131500, 137000, 142500, 148200, 154000, 159900, 165800, 172000, 290000};
}

void TestNaturalLevel()
{
    std::vector<std::uint64_t> const xp = XpTable();

    Check("level 1 to 2 is 400", ExperienceBetween(xp, 1, 0, 2, 0) == 400);
    Check("within a level", ExperienceBetween(xp, 5, 100, 5, 900) == 800);
    Check("backwards is nothing", ExperienceBetween(xp, 10, 0, 9, 0) == 0);

    // The head of the family: 34 when the rate went to ten, 34 to 35 before
    // the boost, then 35 to 60 (167 into it) after the set-back.
    std::uint64_t const earned = ExperienceBetween(xp, 34, 0, 35, 0) + ExperienceBetween(xp, 35, 0, 60, 167);
    NaturalLevel const grug = NaturalLevelFor(xp, 34, 0, earned, 10);
    Check("the head earned 2,765,767 at ten times", earned == 2765767);
    Check("the head's natural level is 38", grug.level == 38);
    Check("...49,076 into it", grug.xpInto == 49076);

    // A Horde character, every level at ten times: 1 to 29 with 35264 into it.
    NaturalLevel const zug = NaturalLevelFor(xp, 1, 0, ExperienceBetween(xp, 1, 0, 29, 35264), 10);
    Check("the Horde head's natural level is 12, 76 into it", zug.level == 12 && zug.xpInto == 76);

    Check("no experience, no levels", NaturalLevelFor(xp, 20, 50, 0, 10).level == 20 &&
                                          NaturalLevelFor(xp, 20, 50, 0, 10).xpInto == 50);
    Check("a rate of one is the experience itself", NaturalLevelFor(xp, 1, 0, 400, 1).level == 2);
    Check("the table's end stops the climb", NaturalLevelFor(xp, 59, 0, 100000000, 1).level == 61);
}

void TestLowerSpells()
{
    LowerSpellFacts expert;
    expert.trainerLevel = 60;
    Check("Expert Riding goes at 38", LowerSpellDecisionFor(expert, 38).verdict == StripVerdict::Remove);
    LowerSpellFacts apprentice;
    apprentice.trainerLevel = 20;
    Check("Apprentice Riding stays at 38", LowerSpellDecisionFor(apprentice, 38).verdict == StripVerdict::Keep);
    LowerSpellFacts plate;
    plate.trainerLevel = 40;
    Check("plate at 40 goes at 38", LowerSpellDecisionFor(plate, 38).verdict == StripVerdict::Remove);
    LowerSpellFacts talent = expert;
    talent.talent = true;
    Check("a talent is left to the talent reset", LowerSpellDecisionFor(talent, 38).verdict == StripVerdict::Keep);
    LowerSpellFacts quest = expert;
    quest.questReward = true;
    Check("a quest's spell is earned at any level", LowerSpellDecisionFor(quest, 38).verdict == StripVerdict::Keep);
    LowerSpellFacts recipe;
    recipe.trainerLevel = 0;
    recipe.recipeRank = 200;
    recipe.tradeMaxAfter = 150;
    Check("a recipe past the trade's new ceiling goes", LowerSpellDecisionFor(recipe, 12).verdict == StripVerdict::Remove);
    recipe.tradeMaxAfter = 225;
    Check("a recipe under it stays", LowerSpellDecisionFor(recipe, 30).verdict == StripVerdict::Keep);
    LowerSpellFacts none;
    Check("a spell no trainer sells stays", LowerSpellDecisionFor(none, 1).verdict == StripVerdict::Keep);

    Check("a weapon skill of 300 at level 38 is 190", LoweredSkillValue(300, 38) == 190);
    Check("a lower one is left", LoweredSkillValue(120, 38) == 120);
}

void TestDues()
{
    Check("a dues source carries its amount", DuesAmountFromSource("guilddues:2500000") == 2500000);
    Check("a walk's source is not dues", DuesAmountFromSource("guilddues-walk:2500000") == 0);
    Check("anything else is not dues", DuesAmountFromSource("guildbank") == 0 &&
                                           DuesAmountFromSource("guilddues:") == 0 &&
                                           DuesAmountFromSource("guilddues:12a") == 0);

    // The Alliance head: eleven letters taken (one of them sent twice by a
    // row that did not read back), three still sealed, 2622g in the purse.
    std::vector<DuesLetter> letters = {
        {24907, 0, 2195985},  {25053, 0, 2500000}, {25056, 0, 2500000}, {25092, 0, 2500000},
        {25137, 0, 2500000},  {25190, 0, 2500000}, {25343, 0, 2500000}, {25384, 0, 2500000},
        {25737, 0, 2500000},  {26276, 0, 2500000}, {26338, 0, 1395658}, {28730, 2500000, 2500000},
        {28731, 2500000, 2500000}, {28794, 1333305, 1333305},
    };
    DuesDiscard const grug = DuesDiscardFor(letters, 26222729);
    Check("the sealed three are emptied", grug.lettersToEmpty == std::vector<unsigned>({28730, 28731, 28794}) &&
                                              grug.unopenedMoney == 6333305);
    Check("the taken eleven come to 2609g 16s 43c", grug.takenMoney == 26091643);
    Check("...and all of it leaves the purse", grug.fromPurse == 26091643);

    DuesDiscard const poor = DuesDiscardFor(letters, 1000000);
    Check("never more than the purse holds", poor.fromPurse == 1000000);

    std::vector<DuesLetter> const unknown = {{1, 0, 0}, {2, 500, 0}};
    DuesDiscard const u = DuesDiscardFor(unknown, 999999);
    Check("a taken letter with no record counts as nothing", u.takenMoney == 0 && u.fromPurse == 0 &&
                                                                 u.takenWithNoRecord == 1);
    Check("a sealed one is still emptied", u.lettersToEmpty == std::vector<unsigned>({2}) && u.unopenedMoney == 500);

    NaturalizeRequest const r = ParseNaturalizeRequest("discard-unearned-gold dry-run");
    Check("the gold mode parses", r.ok && r.mode == NaturalizeMode::DiscardUnearnedGold && r.dryRun &&
                                      r.parts == NATURALIZE_PART_GOLD);
    Check("it takes no level", !ParseNaturalizeRequest("discard-unearned-gold level:30").ok);
    NaturalizeFacts bot = GuildBot();
    Check("a guild bot has no family gold to lose", NaturalizeVerdictFor(r, bot, true) == NaturalizeRefusal::NotFamily);
    Check("the gold part has a ledger word", std::string(NaturalizePartWord(NATURALIZE_PART_GOLD)) == "gold");
}

void TestMail()
{
    Check("no items, no mail", MailsNeededFor(0) == 0);
    Check("twelve fit one mail", MailsNeededFor(12) == 1);
    Check("thirteen need two", MailsNeededFor(13) == 2);
    Check("Grug's two bags, 29 items, need three", MailsNeededFor(29) == 3);
}

}  // namespace

int main()
{
    TestParse();
    TestNameList();
    TestVerdict();
    TestResetKeepsAndRemoves();
    TestResetBotValues();
    TestStripSpells();
    TestStripWeaponSkills();
    TestGmAdditem();
    TestGmAttribution();
    TestBoostAchievements();
    TestNaturalLevel();
    TestLowerSpells();
    TestDues();
    TestMail();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_naturalize: all passed\n");
    return 0;
}
