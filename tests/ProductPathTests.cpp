#include "iupac/domain/ProductInfo.hpp"
#include "iupac/engine/Engine.hpp"

#include <algorithm>
#include <array>
#include <iostream>

bool runPatchTests();
bool runModuleTests();
bool runEngineTests();
bool runHeadlessTests();
bool runRealtimeAllocationTests();
bool runCoordinatorConcurrencyTests();

int main()
{
    iupac::engine::Engine engine;
    engine.prepare(48000.0, 128);

    std::array<float, 128> left;
    std::array<float, 128> right;
    left.fill(1.0f);
    right.fill(-1.0f);
    engine.renderSilence(left, right);

    const auto silent = std::ranges::all_of(left, [](float sample) { return sample == 0.0f; })
        && std::ranges::all_of(right, [](float sample) { return sample == 0.0f; });
    const auto configured = engine.sampleRate() == 48000.0 && engine.maximumBlockSize() == 128;
    const auto identified = iupac::domain::productName() == "IUPAC Synth 2"
        && iupac::domain::architectureVersion() == "3";

    if (! silent || ! configured || ! identified || ! runPatchTests() || ! runModuleTests() || ! runEngineTests() || ! runHeadlessTests() || ! runRealtimeAllocationTests() || ! runCoordinatorConcurrencyTests())
    {
        std::cerr << "production path smoke failed\n";
        return 1;
    }

    return 0;
}
