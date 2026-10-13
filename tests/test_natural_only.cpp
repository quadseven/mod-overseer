/*
 * Natural only: the module grants a member nothing a player could not earn.
 *
 * THE OPERATOR'S STANDING RULE (2026-10-12): no granted levels, gear, skills,
 * buffs, gold, reputation, items, resurrections or teleports for any member,
 * anywhere. A member gains a thing by a loot, a quest, a trainer, a vendor or
 * a kill, or it does not gain it.
 *
 * WHAT THIS CHECKS. Every call in src/ that can hand a character something out
 * of thin air - an item created, a spell or a skill set, a level or experience,
 * money that is not a sale, an aura, a resurrection, a teleport, a reputation
 * set, a bot factory's initialisation, a dot-command - and every SQL string
 * that writes those tables directly. Each one must carry the marker
 *
 *     natural-only: <why it cannot reach a member, or why it is natural>
 *
 * in a comment on the same line or in the comment block directly above it. A
 * call without the marker fails the test. So a grant cannot come back
 * unnoticed: whoever adds one has to write down, next to it, why it is not one,
 * and a reviewer reads that sentence.
 *
 * The scan reads code only: comments and string literals are blanked first, so
 * the prose all over this module that NAMES these calls ("Player::TeleportTo
 * takes a character out of the world...") is not mistaken for a call. SQL is
 * read from the string literals separately.
 *
 * The scanner proves itself on fixed text first, so a scanner that silently
 * stopped matching cannot pass on the real source.
 */

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace
{
char const* const MARKER = "natural-only:";

// One source line, split into the three things the check reads.
struct Line
{
    std::string code;     // comments and string literal contents blanked
    std::string comment;  // the comment text on this line
    std::string strings;  // the string literal contents on this line
};

// Split a file into lines of code, comment and string text. A small C++
// lexer: line and block comments, string and character literals with their
// escapes, and raw strings, which is all the module uses.
std::vector<Line> Lex(std::string const& text)
{
    enum class State { Code, LineComment, BlockComment, String, Char, Raw };
    std::vector<Line> lines(1);
    State state = State::Code;
    std::string rawEnd;  // )delim" for a raw string
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char const c = text[i];
        char const next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (c == '\n')
        {
            lines.emplace_back();
            if (state == State::LineComment)
                state = State::Code;
            continue;
        }
        Line& line = lines.back();
        switch (state)
        {
            case State::Code:
                if (c == '/' && next == '/')
                {
                    state = State::LineComment;
                    ++i;
                }
                else if (c == '/' && next == '*')
                {
                    state = State::BlockComment;
                    ++i;
                }
                else if (c == 'R' && next == '"' &&
                         (i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_')))
                {
                    std::size_t const open = text.find('(', i + 2);
                    if (open == std::string::npos)
                    {
                        line.code += c;
                        break;
                    }
                    rawEnd = ")" + text.substr(i + 2, open - (i + 2)) + "\"";
                    line.code += "R\"";
                    state = State::Raw;
                    i = open;
                }
                else if (c == '"')
                {
                    line.code += c;
                    state = State::String;
                }
                else if (c == '\'')
                {
                    line.code += c;
                    state = State::Char;
                }
                else
                    line.code += c;
                break;
            case State::LineComment:
                line.comment += c;
                break;
            case State::BlockComment:
                if (c == '*' && next == '/')
                {
                    state = State::Code;
                    ++i;
                }
                else
                    line.comment += c;
                break;
            case State::String:
                if (c == '\\' && next != '\0' && next != '\n')
                {
                    line.strings += c;
                    line.strings += next;
                    ++i;
                }
                else if (c == '"')
                {
                    line.code += c;
                    line.strings += ' ';
                    state = State::Code;
                }
                else
                    line.strings += c;
                break;
            case State::Char:
                if (c == '\\' && next != '\0')
                    ++i;
                else if (c == '\'')
                {
                    line.code += c;
                    state = State::Code;
                }
                break;
            case State::Raw:
                if (text.compare(i, rawEnd.size(), rawEnd) == 0)
                {
                    line.code += '"';
                    line.strings += ' ';
                    i += rawEnd.size() - 1;
                    state = State::Code;
                }
                else
                    line.strings += c;
                break;
        }
    }
    return lines;
}

// The calls that can grant. Each matches a call, not a mention: a member
// access or a scope before the name and an opening parenthesis after it.
struct Grant
{
    char const* what;
    std::regex pattern;
};

std::vector<Grant> const& CodeGrants()
{
    static std::vector<Grant> const grants = [] {
        std::vector<Grant> g;
        auto call = [&g](char const* what, char const* names) {
            g.push_back({what, std::regex(std::string("(->|\\.|::)\\s*(") + names + ")\\s*\\(")});
        };
        call("an item created", "AddItem|StoreNewItem|StoreNewItemInBestSlots|EquipNewItem|CreateItem|SendNewItem");
        call("a spell taught", "learnSpell|LearnSpell|addSpell|TeachSpell|LearnDefaultSkill|learnDefaultSpells");
        call("a skill set", "SetSkill|UpdateSkill|UpdateSkillPro");
        call("a level or experience", "SetLevel|GiveLevel|GiveXP");
        call("money", "ModifyMoney|SetMoney");
        call("an aura or a cast", "AddAura|CastSpell|CastCustomSpell");
        call("a resurrection", "ResurrectPlayer|ResurrectUsingRequestData");
        call("a teleport", "TeleportTo|NearTeleportTo|TeleportToEntryPoint");
        call("a reputation set", "SetReputation|SetOneFactionReputation|ModifyReputation");
        call("a dot-command", "ParseCommands");
        call("a talent spec applied", "InitTalentsBySpecNo|InitTalentsTree");
        // The bot factory hands out gear, spells, skills, bags, consumables
        // and mounts wholesale; any use of it at all is the question.
        g.push_back({"the bot factory", std::regex("\\bPlayerbotFactory\\b")});
        // Experience written straight to the field.
        g.push_back({"experience set", std::regex("SetUInt32Value\\s*\\(\\s*PLAYER_XP\\b")});
        return g;
    }();
    return grants;
}

// SQL that writes a character's holdings behind the core's back.
std::regex const& SqlGrant()
{
    static std::regex const sql(
        "\\b(INSERT|REPLACE)\\s+(IGNORE\\s+)?INTO\\s+`?(character_inventory|item_instance|character_spell|"
        "character_skills|character_reputation|character_aura|mail_items|character_queststatus_rewarded|"
        "character_achievement|character_talent)\\b"
        "|\\bUPDATE\\s+`?(characters|character_skills|character_reputation|item_instance|character_spell)\\b",
        std::regex::icase);
    return sql;
}

struct Finding
{
    std::size_t line;  // 1-based
    std::string what;
    std::string text;
};

// A cheap first pass: std::regex is slow, and nearly every line names none
// of these. Every pattern above contains one of these words: "XP" is in
// PLAYER_XP, "Talents" in InitTalentsBySpecNo and InitTalentsTree, "pell" in
// every spell call, and the self-test below finds each pattern through it.
bool MayGrant(Line const& line)
{
    static char const* const code[] = {
        "Item", "pell", "Skill", "Level", "XP", "Money", "Aura", "Resurrect", "Teleport",
        "Reputation", "ParseCommands", "Talents", "PlayerbotFactory"};
    static char const* const sql[] = {"INTO", "into", "Into", "UPDATE", "update", "Update"};
    for (char const* word : code)
        if (line.code.find(word) != std::string::npos)
            return true;
    for (char const* word : sql)
        if (line.strings.find(word) != std::string::npos)
            return true;
    return false;
}

bool Marked(std::vector<Line> const& lines, std::size_t i)
{
    if (lines[i].comment.find(MARKER) != std::string::npos)
        return true;
    // The comment block directly above: comment-only lines, nothing between.
    for (std::size_t j = i; j-- > 0;)
    {
        std::string const& code = lines[j].code;
        if (code.find_first_not_of(" \t\r") != std::string::npos)
            return false;
        if (lines[j].comment.empty())
            return false;
        if (lines[j].comment.find(MARKER) != std::string::npos)
            return true;
    }
    return false;
}

std::vector<Finding> Unmarked(std::string const& text)
{
    std::vector<Line> const lines = Lex(text);
    std::vector<Finding> found;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        std::string what;
        if (!MayGrant(lines[i]))
            continue;
        for (Grant const& g : CodeGrants())
            if (std::regex_search(lines[i].code, g.pattern))
            {
                what = g.what;
                break;
            }
        if (what.empty() && std::regex_search(lines[i].strings, SqlGrant()))
            what = "a direct write to a character table";
        if (!what.empty() && !Marked(lines, i))
            found.push_back({i + 1, what, lines[i].code});
    }
    return found;
}

int failures = 0;

void Expect(bool ok, char const* what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

// The scanner on fixed text, so it cannot pass the source by matching nothing.
void SelfTest()
{
    Expect(Unmarked("    bot->TeleportTo(0, 1.f, 2.f, 3.f, 0.f);\n").size() == 1,
           "a bare teleport is found");
    Expect(Unmarked("    player->AddItem(6159, 1);\n").size() == 1, "a bare item grant is found");
    Expect(Unmarked("    player->learnSpell(133);\n").size() == 1, "a bare spell grant is found");
    Expect(Unmarked("    player->SetLevel(60);\n").size() == 1, "a bare level set is found");
    Expect(Unmarked("    player->ModifyMoney(100);\n").size() == 1, "a bare money grant is found");
    Expect(Unmarked("    bot->AddAura(25661, bot);\n").size() == 1, "a bare aura is found");
    Expect(Unmarked("    bot->ResurrectPlayer(1.0f);\n").size() == 1, "a bare resurrection is found");
    Expect(Unmarked("    rep.SetOneFactionReputation(f, 42000, false);\n").size() == 1,
           "a bare reputation set is found");
    Expect(Unmarked("    PlayerbotFactory factory(bot, 60);\n").size() == 1, "the bot factory is found");
    Expect(Unmarked("    bot->SetUInt32Value(PLAYER_XP, 0);\n").size() == 1, "an experience set is found");
    Expect(Unmarked("    factory.InitTalentsTree(true);\n").size() == 1, "a talent tree applied is found");
    Expect(Unmarked("    factory.InitTalentsBySpecNo(bot, 1, true);\n").size() == 1,
           "a talent spec applied is found");
    Expect(Unmarked("    player->TeachSpell(133);\n").size() == 1, "a spell taught is found");
    Expect(Unmarked("    player->SetSkill(129, 1, 75, 75);\n").size() == 1, "a skill set is found");
    Expect(Unmarked("    handler.ParseCommands(\".additem 1\");\n").size() == 1, "a dot-command is found");
    Expect(Unmarked("    Execute(\"INSERT INTO character_inventory (guid) VALUES (1)\");\n").size() == 1,
           "a direct inventory write is found");
    Expect(Unmarked("    Execute(\"UPDATE characters SET money = 1\");\n").size() == 1,
           "a direct character write is found");

    Expect(Unmarked("    bot->TeleportTo(0, 1.f, 2.f, 3.f, 0.f);  // natural-only: test\n").empty(),
           "a marker on the same line passes");
    Expect(Unmarked("    // natural-only: a GM path\n    // that runs for nobody\n    bot->TeleportTo(0, 1.f);\n")
               .empty(),
           "a marker in the comment block directly above passes");
    Expect(Unmarked("    // natural-only: too far\n    int x = 0;\n    bot->TeleportTo(0, 1.f);\n").size() == 1,
           "a marker separated by code does not pass");
    Expect(Unmarked("    // natural-only: too far\n\n    bot->TeleportTo(0, 1.f);\n").size() == 1,
           "a marker separated by a blank line does not pass");
    Expect(Unmarked("    // Player::TeleportTo(m_homebind) takes it out of the world\n").empty(),
           "a call named in a comment is not a call");
    Expect(Unmarked("    /* bot->AddItem(1, 1);\n       bot->SetLevel(2); */\n").empty(),
           "a call named in a block comment is not a call");
    Expect(Unmarked("    Log(\"refused ->TeleportTo( here\");\n").empty(),
           "a call named in a string is not a call");
    // Each literal closes and the code after it is read again: a lexer that
    // stayed inside one would blank the call that follows.
    Expect(Unmarked("    Log(\"a \\\"quoted\\\" word\"); bot->TeleportTo(0, 1.f);\n").size() == 1,
           "the code after a string literal is read");
    Expect(Unmarked("    char q = '\"'; bot->TeleportTo(0, 1.f);\n").size() == 1,
           "the code after a character literal is read");
    Expect(Unmarked("    Log(R\"x(a \"quote\" inside)x\"); bot->TeleportTo(0, 1.f);\n").size() == 1,
           "the code after a raw string is read");
    Expect(Unmarked("    Query(\"SELECT skill FROM character_skills\");\n").empty(), "a read is not a write");
    Expect(Unmarked("    bool CanStoreNewItem(int);\n    bot->CanStoreNewItem(1);\n").empty(),
           "asking whether an item fits is not a grant");
}
}  // namespace

int main()
{
    SelfTest();

    char const* const files[] = {"src/mod_overseer.cpp", "src/overseer_decisions.cpp",
                                 "src/overseer_decisions.h", "src/overseer_crash.h"};
    for (char const* path : files)
    {
        std::ifstream source(path);
        if (!source)
        {
            std::cerr << "FAIL: cannot read " << path << "\n";
            ++failures;
            continue;
        }
        std::string const text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
        for (Finding const& f : Unmarked(text))
        {
            std::cerr << "FAIL: " << path << ":" << f.line << ": " << f.what
                      << " without a `natural-only:` marker:\n    " << f.text << "\n";
            ++failures;
        }
    }

    if (failures)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "natural only: every grant-shaped call in src/ is marked\n";
    return EXIT_SUCCESS;
}
