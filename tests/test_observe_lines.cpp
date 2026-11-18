/*
 * The camera's own lines on kind='gm'.
 *
 * kind='gm' is retired for every member. The operator's Watcher, a real client
 * on a game master account, is the one character that is not a member, and the
 * control plane moves it with `.appear`, `.instance unbind all`, a hop out of
 * an instance, and two lines that put it in the observer state. These are the
 * rows and the gate: what is an observer line, and who may run one.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>

namespace D = OverseerDecisions;

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

D::ObserveVerb Verb(char const* line, std::string* name = nullptr)
{
    D::ObserveRow row;
    if (!D::ParseObserveRow(line, row))
        return D::ObserveVerb::None;
    if (name)
        *name = row.name;
    return row.verb;
}

}  // namespace

int main()
{
    std::string name;
    Check("appear names its target", Verb(".appear Crag", &name) == D::ObserveVerb::Appear && name == "Crag");
    Check("appear without the dot", Verb("appear Crag") == D::ObserveVerb::Appear);
    Check("appear in capitals and spaces", Verb("  .APPEAR   Crag ") == D::ObserveVerb::Appear);
    Check("appear with no name is not a line", Verb(".appear") == D::ObserveVerb::None);
    Check("appear with two names is not a line", Verb(".appear Crag Krog") == D::ObserveVerb::None);
    Check("appear with a digit in the name is not a line", Verb(".appear Cr4g") == D::ObserveVerb::None);
    Check("appear with a one letter name is not a line", Verb(".appear C") == D::ObserveVerb::None);
    Check("appear with a 13 letter name is not a line", Verb(".appear Abcdefghijklm") == D::ObserveVerb::None);

    Check("unbind all", Verb(".instance unbind all") == D::ObserveVerb::UnbindAll);
    Check("unbind one map is not the line", Verb(".instance unbind 47") == D::ObserveVerb::None);
    Check("the hop", Verb(".tele stormwind") == D::ObserveVerb::Hop);
    Check("a tele elsewhere is not the line", Verb(".tele ironforge") == D::ObserveVerb::None);
    Check("gm on", Verb(".gm on") == D::ObserveVerb::GmOn);
    Check("gm visible off", Verb(".gm visible off") == D::ObserveVerb::GmHidden);
    Check("gm visible on is not the line", Verb(".gm visible on") == D::ObserveVerb::None);

    // The lines a grant would use are not the camera's, whatever the character.
    Check("additem is not an observer line", Verb(".additem 6948 1") == D::ObserveVerb::None);
    Check("levelup is not an observer line", Verb(".levelup 10") == D::ObserveVerb::None);
    Check("modify money is not an observer line", Verb(".modify money 1000") == D::ObserveVerb::None);
    Check("an empty row is not a line", Verb("") == D::ObserveVerb::None);
    Check("a prefix is not the line", Verb(".appearance Crag") == D::ObserveVerb::None);

    D::ObserveFacts camera;
    camera.gameMasterAccount = true;
    Check("a real client on a game master account may", std::strlen(D::ObserveGate(camera)) == 0);

    D::ObserveFacts bot = camera;
    bot.isBot = true;
    Check("a bot may not, even on a game master account",
          std::strcmp(D::ObserveGate(bot), D::ObserveRefusal::NotAClient) == 0);

    D::ObserveFacts player;
    Check("a player account may not",
          std::strcmp(D::ObserveGate(player), D::ObserveRefusal::NotAGameMaster) == 0);

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
