/*
 * Source contract for the Discord order poll (mod-overseer#150).
 *
 * The order lookups query item_template and map_dbc by substring, which blocks
 * on the database. DriveOrders runs inside the world update, so it may only
 * hand the work to a worker and never read the database itself.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(std::string const& what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}

std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}
}  // namespace

int main(int argc, char** argv)
{
    std::ifstream in(argc > 1 ? argv[1] : "src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string const drive = Body(text, "    void DriveOrders()");
    if (drive.empty())
        return Fail("DriveOrders not found");
    if (drive.find("Database.Query") != std::string::npos)
        return Fail("DriveOrders reads the database on the world thread");
    if (drive.find("std::async(std::launch::async") == std::string::npos)
        return Fail("DriveOrders does not hand the read to a worker");

    std::string const run = Body(text, "    static void RunOrders(");
    if (run.find("overseer_order_inbox") == std::string::npos)
        return Fail("RunOrders does not read the inbox");
    return EXIT_SUCCESS;
}
