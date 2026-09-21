#include "iupac/engine/PatchCoordinator.hpp"

#include <algorithm>
#include <cmath>

namespace iupac::engine
{
PatchCoordinator::PatchCoordinator() = default;

void PatchCoordinator::prepare(double sampleRate, std::size_t maximumBlockSize)
{
    maximumBlockSize_ = std::max<std::size_t>(1, maximumBlockSize);
    fadeLength_ = static_cast<std::uint64_t>(std::max(1.0, sampleRate * .020));
    for (auto& bank : banks_) bank.prepare(sampleRate, maximumBlockSize_);
    for (auto& buffer : scratch_) buffer.resize(maximumBlockSize_);
    bend_.fill(8192);
    clearEffective();
    prepared_ = true;
}

void PatchCoordinator::clearEffective() noexcept
{
    effective_.nodeCount.store(0, std::memory_order_relaxed);
    for (std::size_t n = 0; n < domain::maximumNodes; ++n) {
        effective_.nodeIds[n].store(0, std::memory_order_relaxed);
        for (auto& value : effective_.values[n]) value.store(-1.0f, std::memory_order_relaxed);
    }
}

// Once per host block: the audible bank reports the newest-started voice's post-summation, post-clamp
// normalized values (or the zero-modulation base with no voice) and they are stored with relaxed atomics.
void PatchCoordinator::publishEffective() noexcept
{
    banks_[active_].effectiveValues(effectiveScratch_);
    for (std::size_t n = 0; n < domain::maximumNodes; ++n) {
        effective_.nodeIds[n].store(effectiveScratch_.nodeIds[n], std::memory_order_relaxed);
        for (std::size_t t = 0; t < parameterTargetCount; ++t) effective_.values[n][t].store(effectiveScratch_.values[n][t], std::memory_order_relaxed);
    }
    effective_.nodeCount.store(effectiveScratch_.nodeCount, std::memory_order_relaxed);
}

EffectiveValues PatchCoordinator::effectiveValues() const noexcept
{
    EffectiveValues out;
    out.nodeCount = effective_.nodeCount.load(std::memory_order_relaxed);
    for (std::size_t n = 0; n < domain::maximumNodes; ++n) {
        out.nodeIds[n] = effective_.nodeIds[n].load(std::memory_order_relaxed);
        for (std::size_t t = 0; t < parameterTargetCount; ++t) out.values[n][t] = effective_.values[n][t].load(std::memory_order_relaxed);
    }
    return out;
}

std::uint64_t PatchCoordinator::publish(const CompiledPatch& patch, const domain::HostControls& controls) noexcept
{
    const auto generation = nextGeneration_.fetch_add(1, std::memory_order_relaxed) + 1;
    producerPending_ = Command{patch, controls, generation};
    enqueuePending();
    return generation;
}

bool PatchCoordinator::publishIfCurrent(std::uint64_t authorGeneration, const CompiledPatch& patch, const domain::HostControls& controls) noexcept
{
    if (authorGeneration != nextGeneration_.load(std::memory_order_acquire)) return false;
    (void) publish(patch, controls);
    return true;
}

bool PatchCoordinator::retryPending() noexcept { return enqueuePending(); }
void PatchCoordinator::invalidateAsyncAuthors() noexcept { nextGeneration_.fetch_add(1, std::memory_order_relaxed); }

bool PatchCoordinator::enqueuePending() noexcept
{
    if (!producerPending_) return true;
    const auto scope = fifo_.write(1);
    if (scope.blockSize1 == 0) return false;
    commands_[static_cast<std::size_t>(scope.startIndex1)] = *producerPending_;
    producerPending_.reset();
    return true;
}

bool PatchCoordinator::structural(const CompiledPatch& a, const CompiledPatch& b) const noexcept
{
    if (a.nodeCount != b.nodeCount || a.edgeCount != b.edgeCount || a.tailStart != b.tailStart) return true;
    for (std::size_t i = 0; i < a.nodeCount; ++i)
        if (a.nodes[i].type != b.nodes[i].type || a.nodes[i].idHash != b.nodes[i].idHash
            || a.nodes[i].values.mode != b.nodes[i].values.mode || a.nodes[i].values.color != b.nodes[i].values.color) return true;
    for (std::size_t i = 0; i < a.edgeCount; ++i) {
        const auto& x = a.edges[i]; const auto& y = b.edges[i];
        if (x.source != y.source || x.destination != y.destination || x.toOutput != y.toOutput || x.port != y.port) return true;
    }
    return false;
}

void PatchCoordinator::seedIncoming() noexcept
{
    std::array<MidiEvent, maximumVoices + 48> events{};
    std::size_t count = 0;
    for (std::size_t ch = 0; ch < 16; ++ch) {
        events[count++] = {0, MidiEventType::pitchBend, static_cast<std::uint8_t>(ch + 1), 0, 0, bend_[ch]};
        events[count++] = {0, MidiEventType::controlChange, static_cast<std::uint8_t>(ch + 1), 1, cc1_[ch], 8192};
        events[count++] = {0, MidiEventType::controlChange, static_cast<std::uint8_t>(ch + 1), 64, static_cast<std::uint8_t>(sustain_[ch] ? 127 : 0), 8192};
    }
    const auto controllerCount = count;
    for (std::size_t ch = 0; ch < 16 && count < events.size(); ++ch)
        for (std::size_t note = 0; note < 128 && count < events.size(); ++note)
            for (std::size_t n = 0; n < static_cast<std::size_t>(heldCount_[ch][note] + sustainedCount_[ch][note]) && count - controllerCount < maximumVoices; ++n)
                events[count++] = {0, MidiEventType::noteOn, static_cast<std::uint8_t>(ch + 1), static_cast<std::uint8_t>(note), velocities_[ch][note], 8192};
    std::array<float, 1> left{}, right{};
    banks_[incoming_].render(left, right, std::span<const MidiEvent>(events.data(), count));
}

void PatchCoordinator::accept(const Command& command) noexcept
{
    accepted_.store(command.generation, std::memory_order_release);
    if (!prepared_) return;
    if (!structural(activePatch_, command.patch)) {
        activePatch_ = command.patch; activeControls_ = command.controls;
        banks_[active_].updatePatchPreservingVoices(command.patch); banks_[active_].setControls(command.controls);
        if (fading_) { banks_[incoming_].updatePatchPreservingVoices(command.patch); banks_[incoming_].setControls(command.controls); }
        audible_.store(command.generation, std::memory_order_release);
        return;
    }
    if (fading_) { audioDeferred_ = command; return; }
    incoming_ = 1 - active_;
    banks_[incoming_].reset(); banks_[incoming_].setControls(command.controls); banks_[incoming_].setPatch(command.patch);
    seedIncoming();
    activePatch_ = command.patch; activeControls_ = command.controls;
    fadeSample_ = 0; transitionGeneration_ = command.generation; fading_ = true; transitioning_.store(true, std::memory_order_release);
    clearEffective();
}

void PatchCoordinator::remember(std::span<const MidiEvent> events) noexcept
{
    for (const auto& event : events) {
        if (event.channel < 1 || event.channel > 16 || event.data1 > 127 || event.data2 > 127) continue;
        auto& count = heldCount_[event.channel - 1][event.data1];
        auto& sustained = sustainedCount_[event.channel - 1][event.data1];
        if (event.type == MidiEventType::noteOn && event.data2) { count = static_cast<std::uint8_t>(std::min<int>(255, count + 1)); velocities_[event.channel - 1][event.data1] = event.data2; }
        else if (event.type == MidiEventType::noteOff || (event.type == MidiEventType::noteOn && !event.data2)) { if (count) { --count; if (sustain_[event.channel - 1]) sustained = static_cast<std::uint8_t>(std::min<int>(255, sustained + 1)); } }
        else if (event.type == MidiEventType::pitchBend) bend_[event.channel - 1] = event.bend;
        else if (event.type == MidiEventType::controlChange && event.data1 == 1) cc1_[event.channel - 1] = event.data2;
        else if (event.type == MidiEventType::controlChange && event.data1 == 64) { sustain_[event.channel - 1] = event.data2 >= 64; if (!sustain_[event.channel - 1]) sustainedCount_[event.channel - 1].fill(0); }
        else if (event.type == MidiEventType::controlChange && (event.data1 == 120 || event.data1 == 123)) { heldCount_[event.channel - 1].fill(0); sustainedCount_[event.channel - 1].fill(0); }
    }
}

void PatchCoordinator::render(std::span<float> left, std::span<float> right, std::span<const MidiEvent> events) noexcept
{
    Command newest{}; bool have = false;
    for (;;) {
        const auto scope = fifo_.read(queueSlots - 1);
        if (scope.blockSize1 + scope.blockSize2 == 0) break;
        auto take = [&](int start, int count) { for (int i = 0; i < count; ++i) { const auto& c = commands_[static_cast<std::size_t>(start + i)]; if (!have || c.generation > newest.generation) { newest = c; have = true; } } };
        take(scope.startIndex1, scope.blockSize1); take(scope.startIndex2, scope.blockSize2);
    }
    if (have) accept(newest);
    if (hasAudioControls_) { activeControls_ = audioControls_; banks_[active_].setControls(audioControls_); if (fading_) banks_[incoming_].setControls(audioControls_); }
    remember(events);
    const auto total = std::min(left.size(), right.size());
    std::size_t offset = 0;
    while (offset < total) {
        const auto count = std::min(maximumBlockSize_, total - offset);
        auto outL = left.subspan(offset, count), outR = right.subspan(offset, count);
        banks_[active_].render(outL, outR, events);
        if (fading_) {
            auto inL = std::span<float>(scratch_[0].data(), count), inR = std::span<float>(scratch_[1].data(), count);
            banks_[incoming_].render(inL, inR, events);
            for (std::size_t i = 0; i < count; ++i) { const float x = std::min(1.0f, static_cast<float>(fadeSample_ + i) / static_cast<float>(fadeLength_)); outL[i] = std::lerp(outL[i], inL[i], x); outR[i] = std::lerp(outR[i], inR[i], x); }
            fadeSample_ += count;
            if (fadeSample_ >= fadeLength_) { active_ = incoming_; fading_ = false; transitioning_.store(false, std::memory_order_release); audible_.store(transitionGeneration_, std::memory_order_release); if (audioDeferred_) { auto deferred = *audioDeferred_; audioDeferred_.reset(); accept(deferred); } }
        }
        offset += count;
        events = {};
    }
    activeVoices_.store(banks_[active_].activeVoiceCount() + (fading_ ? banks_[incoming_].activeVoiceCount() : 0u), std::memory_order_relaxed);
    if (prepared_) publishEffective();
}

void PatchCoordinator::setAudioControls(const domain::HostControls& controls) noexcept
{
    audioControls_ = controls;
    hasAudioControls_ = true;
}

PatchCoordinator::Status PatchCoordinator::status() const noexcept
{
    return {accepted_.load(std::memory_order_acquire), audible_.load(std::memory_order_acquire), transitioning_.load(std::memory_order_acquire)};
}

std::string ControlCoordinator::publishLocked()
{
    const auto compiled = compilePatch(state_.editedPatch);
    if (!compiled) return compiled.error;
    (void) runtime_.publish(compiled.patch, state_.controls);
    return {};
}

std::string ControlCoordinator::load(domain::State state)
{
    if (auto error = domain::validate(state.basePatch); !error.empty()) return error;
    if (auto error = domain::validate(state.editedPatch); !error.empty()) return error;
    std::scoped_lock lock(stateMutex_); state_ = std::move(state); runtime_.invalidateAsyncAuthors(); return publishLocked();
}

std::string ControlCoordinator::edit(domain::Patch patch)
{
    if (auto error = domain::validate(patch); !error.empty()) return error;
    std::scoped_lock lock(stateMutex_); state_.editedPatch = std::move(patch); state_.provenance.reset(); runtime_.invalidateAsyncAuthors(); return publishLocked();
}

std::string ControlCoordinator::resetEdits()
{
    std::scoped_lock lock(stateMutex_); state_.editedPatch = state_.basePatch; runtime_.invalidateAsyncAuthors(); return publishLocked();
}

std::string ControlCoordinator::resetControls()
{
    std::scoped_lock lock(stateMutex_); state_.controls = {};
    for (std::size_t i = 0; i < state_.controls.macros.size(); ++i) state_.controls.macros[i] = state_.editedPatch.macros[i].defaultValue;
    runtime_.invalidateAsyncAuthors(); return publishLocked();
}

domain::State ControlCoordinator::snapshot() const { std::scoped_lock lock(stateMutex_); return state_; }
}
