#include <cstdlib>

// tests/test_share_refusal_contract.py pins the share verb's refusal words
// against the adapter's source. It had no caller until this wrapper, so the
// gate never ran it. The test loops discover test_*.cpp on their own.
int main()
{
    return std::system("python3 -B tests/test_share_refusal_contract.py") == 0
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
