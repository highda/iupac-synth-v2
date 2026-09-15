#pragma once

#include "iupac/engine/Engine.hpp"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <mutex>
#include <vector>

namespace iupac::engine
{
class PatchCoordinator final
{
public:
    struct Status { std::uint64_t accepted{}, audible{}; bool transitioning{}; };
    PatchCoordinator();
    void prepare(double sampleRate, std::size_t maximumBlockSize);
    [[nodiscard]] std::uint64_t publish(const CompiledPatch&, const domain::HostControls&) noexcept;
    [[nodiscard]] bool publishIfCurrent(std::uint64_t authorGeneration, const CompiledPatch&, const domain::HostControls&) noexcept;
    [[nodiscard]] bool retryPending() noexcept;
    void invalidateAsyncAuthors() noexcept;
    [[nodiscard]] std::uint64_t currentGeneration() const noexcept { return nextGeneration_.load(); }
    void render(std::span<float>, std::span<float>, std::span<const MidiEvent> = {}) noexcept;
    void setAudioControls(const domain::HostControls&) noexcept;
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] std::size_t activeBanks() const noexcept { return transitioning_.load(std::memory_order_acquire) ? 2u : 1u; }
private:
    struct Command { CompiledPatch patch{}; domain::HostControls controls{}; std::uint64_t generation{}; };
    static constexpr int queueSlots = 5;
    bool enqueuePending() noexcept;
    void accept(const Command&) noexcept;
    [[nodiscard]] bool structural(const CompiledPatch&, const CompiledPatch&) const noexcept;
    void remember(std::span<const MidiEvent>) noexcept;
    void seedIncoming() noexcept;
    juce::AbstractFifo fifo_{queueSlots};
    std::array<Command, queueSlots> commands_{};
    std::optional<Command> producerPending_;
    std::optional<Command> audioDeferred_;
    std::array<Engine, 2> banks_{};
    std::array<std::vector<float>, 4> scratch_{};
    std::array<std::array<std::uint8_t, 128>, 16> heldCount_{}, sustainedCount_{}, velocities_{};
    std::array<std::uint8_t, 16> cc1_{};
    std::array<std::uint16_t, 16> bend_{};
    std::array<bool, 16> sustain_{};
    CompiledPatch activePatch_{};
    domain::HostControls activeControls_{};
    domain::HostControls audioControls_{};
    bool hasAudioControls_{};
    std::atomic<std::uint64_t> nextGeneration_{0}, accepted_{0}, audible_{0};
    std::size_t active_{}, incoming_{1}, maximumBlockSize_{};
    std::uint64_t fadeSample_{}, fadeLength_{1}, transitionGeneration_{};
    std::atomic<bool> transitioning_{false};
    bool prepared_{}, fading_{};
};

class ControlCoordinator final
{
public:
    void prepare(double sampleRate, std::size_t maximumBlockSize) { runtime_.prepare(sampleRate, maximumBlockSize); }
    [[nodiscard]] std::string load(domain::State);
    [[nodiscard]] std::string edit(domain::Patch);
    [[nodiscard]] std::string resetEdits();
    [[nodiscard]] std::string resetControls();
    [[nodiscard]] domain::State snapshot() const;
    void render(std::span<float> left, std::span<float> right, std::span<const MidiEvent> events = {}) noexcept { runtime_.render(left, right, events); }
    [[nodiscard]] PatchCoordinator::Status status() const noexcept { return runtime_.status(); }
private:
    [[nodiscard]] std::string publishLocked();
    mutable std::mutex stateMutex_;
    domain::State state_{};
    PatchCoordinator runtime_;
};
}
