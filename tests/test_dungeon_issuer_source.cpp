/*
 * Source contract for AuthorizedDcIssuer's documented selection order.
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
    std::size_t const signature = text.find("static Player* AuthorizedDcIssuer(Player* bot)");
    if (signature == std::string::npos)
        return Fail("cannot find AuthorizedDcIssuer");

    std::size_t const commentStart = text.rfind("    // A groupmate", signature);
    std::string const comment = commentStart == std::string::npos
                                    ? std::string()
                                    : text.substr(commentStart, signature - commentStart);
    if (comment.find("Returns null only when nobody qualifies") == std::string::npos ||
        comment.find("may return the bot itself") == std::string::npos ||
        comment.find("bot is the leader") == std::string::npos)
        return Fail("AuthorizedDcIssuer comment must document its actual null and self cases");
    if (comment.find("because the bot itself is precisely the issuer that is always rejected") !=
        std::string::npos)
        return Fail("stale AuthorizedDcIssuer comment claims the bot can never be returned");

    std::cout << "AuthorizedDcIssuer comment matches its selection contract\n";
    return EXIT_SUCCESS;
}
