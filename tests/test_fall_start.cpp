#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FindFallStart;
using OverseerDecisions::FallTraceSample;
using OverseerDecisions::MoveGenerator;

int main()
{
    FallTraceSample early{1, 10.f, 20.f, 100.f, MoveGenerator::Follow};
    FallTraceSample later{1, 11.f, 21.f, 80.f, MoveGenerator::Thrown};
    auto const found = FindFallStart({early, later}, 1, 70.f, 10.f);
    if (!found.found || found.sample.z != 100.f ||
        found.sample.movement != MoveGenerator::Follow)
        return EXIT_FAILURE;
    if (FindFallStart({early}, 2, 70.f, 10.f).found)
        return EXIT_FAILURE;
    std::puts("fall start selection: all checks passed");
    return EXIT_SUCCESS;
}
