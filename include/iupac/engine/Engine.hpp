#pragma once

#include <cstddef>
#include <span>

namespace iupac::engine
{
class Engine final
{
public:
    void prepare(double sampleRate, std::size_t maximumBlockSize) noexcept;
    void renderSilence(std::span<float> left, std::span<float> right) noexcept;

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return maximumBlockSize_; }

private:
    double sampleRate_ = 0.0;
    std::size_t maximumBlockSize_ = 0;
};
}
