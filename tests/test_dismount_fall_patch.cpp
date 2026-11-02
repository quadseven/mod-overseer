// The dismount fall patch is replayed through the Python harness, which
// compiles the patched pinned source against a port of MotionMaster::MoveFall.
// tests/replay_dismount_fall.py says what each scenario proves. Run from the
// repository root.
#include <cstdlib>
#include <iostream>

int main()
{
    if (std::system("python3 -B tests/replay_dismount_fall.py") == 0)
        return EXIT_SUCCESS;
    std::cerr << "dismount fall replay failed\n";
    return EXIT_FAILURE;
}
