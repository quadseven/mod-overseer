/* Source contract for the plane hearth refusal severity (see
 * test_plane_hearth_refusal.cpp): HearthOffThePlane asks
 * PlaneHearthRefusalClearsItself and logs a refusal that clears itself below
 * ERROR, and keeps ERROR for the rest.
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

    std::size_t const start = text.find("PlaneHearthVerdict HearthOffThePlane(");
    std::size_t const end = text.find("WAS ON THE HIDDEN PLANE", start);
    if (start == std::string::npos || end == std::string::npos)
        return Fail("cannot find HearthOffThePlane up to its cast line");
    std::string const body = text.substr(start, end - start);

    std::size_t const ask = body.find("OverseerDecisions::PlaneHearthRefusalClearsItself(refused)");
    std::size_t const error = body.find("LOG_ERROR(");
    std::size_t const warn = body.find("LOG_WARN(");
    if (ask == std::string::npos)
        return Fail("the refusal must be asked whether it clears itself");
    if (warn == std::string::npos || warn < ask)
        return Fail("a refusal that clears itself is logged below ERROR");
    if (error == std::string::npos)
        return Fail("a refusal that does not clear keeps ERROR");

    std::cout << "plane hearth refusal source: ok\n";
    return EXIT_SUCCESS;
}
