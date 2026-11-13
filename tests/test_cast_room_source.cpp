/*
 * The cast executor asks the core's room, tool and reagent questions before it
 * sends anything, and listens to the core's answer while it does (#1531).
 *
 * Reads src/mod_overseer.cpp as text: the wiring lives in the adapter, which
 * this binary cannot compile.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string Body(std::string const& src, char const* signature, char const* close)
{
    std::size_t const at = src.find(signature);
    if (at == std::string::npos)
        return std::string();
    std::size_t const end = src.find(close, at);
    return src.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}

std::size_t At(std::string const& hay, char const* needle)
{
    return hay.find(needle);
}

} // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    std::string const room =
        Body(src, "static bool CastHasRoomForWhatItMakes(", "\n}\n");
    Check("the room helper asks the core's own store question",
          Has(room, "CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemType, count)"));
    Check("the room helper asks the free slots a random pick needs",
          Has(room, "GetFreeInventorySpace()"));
    Check("the room helper leaves the verdict to the decision",
          Has(room, "OverseerDecisions::CastProductHasRoom(facts)"));

    std::string const tools = Body(src, "static bool CastHasItsTools(", "\n}\n");
    Check("the tool helper asks Totem and TotemCategory",
          Has(tools, "HasItemCount(spell->Totem[i])")
              && Has(tools, "HasItemTotemCategory(spell->TotemCategory[i])"));

    std::string const cast = Body(src, "    static char const* DoCast(", "\n    }\n\n    // WHERE A CAST");
    Check("DoCast found", !cast.empty());
    std::size_t const roomAsk = At(cast, "!CastHasRoomForWhatItMakes(who, spell)");
    std::size_t const toolAsk = At(cast, "!CastHasItsTools(who, spell)");
    std::size_t const reagentAsk = At(cast, "!CastHasAllItsReagents(who, spell)");
    std::size_t const hold = At(cast, "HoldStillAndReport(who, ev.character, \"cast\", ev.hold);\n\n");
    std::size_t const packet = At(cast, "session->HandleCastSpellOpcode(raw);");
    Check("the room is asked", roomAsk != std::string::npos);
    Check("the tool is asked", toolAsk != std::string::npos);
    Check("every reagent is asked", reagentAsk != std::string::npos);
    Check("all three are asked before the hold and the packet",
          roomAsk < hold && toolAsk < hold && reagentAsk < hold && hold < packet);
    Check("the room refusal names the bags", Has(cast, "return refuse(Refusal::NoBagRoom);"));
    Check("the tool refusal names the tool", Has(cast, "return refuse(Refusal::MissingTool);"));

    std::size_t const listen = At(cast, "CastAnswerListener const listening(who->GetGUID(), ev.spellId);");
    Check("the executor listens to the core while the packet is driven",
          listen != std::string::npos && listen < packet);
    Check("the core's answer is kept", Has(cast, "ev.coreResult = listening.Heard();"));
    Check("a core answer is the blocker before the walls",
          Has(cast, "!ev.coreResult.empty() ? OverseerDecisions::CastWall::CoreRefused"));

    Check("the JSON carries the core answer", Has(src, ",\\\"core_result\\\":\" << J(ev.coreResult)"));

    std::string const hook = Body(src, "class OverseerCastFailureScript", "\n};\n");
    std::size_t const hear = At(hook, "CastAnswerListener::Hear(caster, spellId, result);");
    std::size_t const roster = At(hook, "!OnRoster(caster->GetName())");
    Check("the hook hears every caster before the roster filter",
          hear != std::string::npos && roster != std::string::npos && hear < roster);

    std::string const resolve = Body(src, "    void ResolveCastChecks(uint32 elapsedMs)", "\n    }\n\n");
    Check("a nothing verdict asks CastNothingDetail",
          Has(resolve, "OverseerDecisions::CastNothingDetail("));

    std::string const craft = Body(src, "    void DriveCraft()\n", "\n    }\n\n");
    std::size_t const craftRoom = At(craft, "!CastHasRoomForWhatItMakes(bot, info)");
    std::size_t const craftCast = At(craft, "bot->CastSpell(bot, spellId, false);");
    Check("the craft drive asks the room before it casts",
          craftRoom != std::string::npos && craftCast != std::string::npos && craftRoom < craftCast);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok - the cast executor asks for room and hears the core's answer\n");
    return 0;
}
