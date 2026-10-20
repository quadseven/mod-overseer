#include "overseer_decisions.h"

#include <cassert>
#include <string>

int main()
{
    auto fields = OverseerDecisions::CastFailFields(
        1234, "Example Spell", "SPELL_FAILED_EQUIPPED_ITEM_CLASS");
    assert(std::string(fields.kind) == "cast_fail");
    assert(fields.subjectId == 1234);
    assert(fields.subjectName == "Example Spell");
    assert(fields.detail == "SPELL_FAILED_EQUIPPED_ITEM_CLASS");
    assert(fields.source.empty());
}
