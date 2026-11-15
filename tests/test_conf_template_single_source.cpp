/*
 * The conf template has one copy.
 *
 * conf/ holds mod_overseer.conf.dist and nothing else. A second file there
 * (a sed -i backup, a merge leftover) is a stale duplicate a reader can land
 * on, and one such file once shipped with unresolved conflict markers.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

int main()
{
    int failures = 0;
    int files = 0;
    for (auto const& entry : fs::directory_iterator("conf"))
    {
        if (!entry.is_regular_file())
            continue;
        ++files;
        std::string const name = entry.path().filename().string();
        if (name != "mod_overseer.conf.dist")
        {
            std::cerr << "FAIL conf/" << name << " is not the one template\n";
            ++failures;
        }
        std::ifstream in(entry.path());
        std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (char const* marker : {"\n<<<<<<< ", "\n=======\n", "\n>>>>>>> "})
            if (text.find(marker) != std::string::npos)
            {
                std::cerr << "FAIL conf/" << name << " holds a conflict marker\n";
                ++failures;
                break;
            }
    }
    if (files == 0)
    {
        std::cerr << "FAIL conf/ is empty or unreadable\n";
        return EXIT_FAILURE;
    }
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
