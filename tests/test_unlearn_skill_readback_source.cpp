/*
 * Source contract for the unlearn-skill adapter read-back (#1556): the row
 * clears the skill, reads HasSkill again, and only then reports "unlearned".
 * A skill that is still held is refused and never reported as applied.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const fn = text.find("static char const* DoUnlearnSkill(");
    if (fn == std::string::npos)
        return Fail("DoUnlearnSkill must exist");
    std::size_t const end = text.find("static char const* DoLearnRecipe(", fn);
    if (end == std::string::npos)
        return Fail("cannot find the end of DoUnlearnSkill");
    std::string const body = text.substr(fn, end - fn);

    std::size_t const clear = body.find("SetSkill(static_cast<uint16>(request.skill), 0, 0, 0)");
    if (clear == std::string::npos)
        return Fail("the row must clear the skill with SetSkill(skill, 0, 0, 0)");
    std::size_t const readBack = body.find("who->HasSkill(request.skill)", clear);
    if (readBack == std::string::npos)
        return Fail("the row must read HasSkill back after SetSkill");
    std::size_t const stillHeld = body.find("R::StillHeld", readBack);
    if (stillHeld == std::string::npos)
        return Fail("a skill still held after SetSkill must be refused as StillHeld");
    std::size_t const applied = body.find("status = \"applied\"", stillHeld);
    std::size_t const unlearned = body.find("finish(\"unlearned\"", stillHeld);
    if (applied == std::string::npos || unlearned == std::string::npos || applied > unlearned)
        return Fail("applied and the unlearned outcome must follow the StillHeld refusal");
    // The only path that marks the row applied after a clear is past the read-back.
    if (body.find("status = \"applied\"", clear) != applied)
        return Fail("no applied status may sit between the clear and the read-back refusal");
    return EXIT_SUCCESS;
}
