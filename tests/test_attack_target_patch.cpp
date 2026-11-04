// The attack target patch is replayed through the Python harness, which
// compiles the real AttackAction::Attack body from the pinned fixture, with
// and without the patch, against a lifetime model of the map store.
// tests/replay_attack_target.py says what each scenario proves. Run from the
// repository root.
#include <cstdlib>
#include <iostream>

int main()
{
    if (std::system("python3 -B tests/replay_attack_target.py") == 0)
        return EXIT_SUCCESS;
    std::cerr << "attack target replay failed\n";
    return EXIT_FAILURE;
}
