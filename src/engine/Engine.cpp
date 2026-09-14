#include "iupac/engine/Engine.hpp"

#include <algorithm>

namespace iupac::engine
{
void Engine::prepare(const double sampleRate, const std::size_t maximumBlockSize) noexcept
{
    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
}
void Engine::renderSilence(std::span<float> left, std::span<float> right) noexcept
{
    std::ranges::fill(left, 0.0f);
    std::ranges::fill(right, 0.0f);
}
}
