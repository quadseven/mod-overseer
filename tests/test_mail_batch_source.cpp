#include <cstdlib>

// The existing CI and local test loops discover test_*.cpp automatically.
// Run the adapter-fragment replay there without changing workflow files.
int main()
{
    return std::system("python3 -B tests/replay_mail_batch.py") == 0
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
