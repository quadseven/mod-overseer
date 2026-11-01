#include <cstdio>
#include <cstdlib>
int main()
{
    if (std::system("python3 -B tests/replay_disenchant.py") == 0)
        return 0;
    std::puts("FAIL actual disenchant adapter, inventory read-back and bound command SQL");
    return 1;
}
