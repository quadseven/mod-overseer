#include <cstdlib>

// Both canonical test loops discover this wrapper without workflow changes.
int main()
{
    return std::system("python3 -B tests/replay_errand_death_owner.py") == 0
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
