#include "iupac/engine/PatchCoordinator.hpp"
#include "MaximalPatch.hpp"

#include <array>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <memory>
#include <vector>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif

namespace {
thread_local bool auditRealtimeMemory = false;
thread_local std::size_t realtimeAllocations = 0;
thread_local std::size_t realtimeFrees = 0;

void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t))
{
    if (auditRealtimeMemory) ++realtimeAllocations;
    void* result = nullptr;
    if (alignment <= alignof(std::max_align_t)) result = std::malloc(size);
    else if (posix_memalign(&result, alignment, size) != 0) result = nullptr;
    if (!result) throw std::bad_alloc{};
    return result;
}

void release(void* pointer) noexcept
{
    if (auditRealtimeMemory && pointer) ++realtimeFrees;
    std::free(pointer);
}

// Heap bytes currently in use by this process; JUCE buffers go through malloc/calloc rather
// than operator new, so the allocator's own accounting is the honest measure.
std::size_t heapBytesInUse()
{
#if defined(__APPLE__)
    malloc_statistics_t statistics{}; malloc_zone_statistics(nullptr, &statistics); return statistics.size_in_use;
#elif defined(__linux__)
    return static_cast<std::size_t>(mallinfo2().uordblks) + static_cast<std::size_t>(mallinfo2().hblkhd);
#else
    return 0;
#endif
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocate(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocate(size, static_cast<std::size_t>(alignment)); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }

bool runRealtimeAllocationTests()
{
    using namespace iupac;
    // Fully populated patch: every slot active, maximumEdges edges and maximumMatrixRows rows.
    const auto patch = iupac::testing::maximalPatch();
    const auto compiled = engine::compilePatch(patch);
    if (!compiled || compiled.patch.nodeCount != domain::maximumNodes || compiled.patch.edgeCount != domain::maximumEdges || compiled.patch.rowCount != domain::maximumMatrixRows) {
        std::cerr << "realtime allocation test failed: maximal patch did not compile fully\n";
        return false;
    }

    // Working storage for sixteen voices x two banks plus the command FIFO: the coordinator object
    // itself plus every byte the heap gains while it is constructed *and* prepared. Construction is
    // inside the window because each bank's Engine::Impl — which owns the sixteen voices and all
    // their per-module state, including the D8 seven-copy unison oscillator banks (#120) — is
    // allocated there, not in prepare(). Measuring from after construction, as this gate did before
    // #120, silently omitted the per-voice module storage the unison work grows.
    const auto heapBefore = heapBytesInUse();
    auto coordinator = std::make_unique<engine::PatchCoordinator>();
    coordinator->prepare(48000.0, 128);
    const auto heapAfter = heapBytesInUse();
    const auto workingStorage = sizeof(engine::PatchCoordinator) + (heapAfter > heapBefore ? heapAfter - heapBefore : 0);
    constexpr std::size_t workingStorageBound = 128u * 1024u * 1024u;
    std::cout << "prepared working storage: " << workingStorage << " bytes (" << (workingStorage / (1024.0 * 1024.0)) << " MiB of the 128 MiB bound; sizeof(PatchCoordinator) "
              << sizeof(engine::PatchCoordinator) << ", sizeof(CompiledPatch) " << sizeof(engine::CompiledPatch) << ")\n";
    if (workingStorage > workingStorageBound) {
        std::cerr << "realtime allocation test failed: prepared working storage exceeds 128 MiB\n";
        return false;
    }
    (void) coordinator->publish(compiled.patch, {});
    std::array<float, 128> left{}, right{};
    std::vector<engine::MidiEvent> events;
    for (std::uint8_t note = 48; note < 48 + engine::maximumVoices; ++note) events.push_back({0, engine::MidiEventType::noteOn, 1, note, 100, 8192});

    realtimeAllocations = realtimeFrees = 0;
    auditRealtimeMemory = true;
    coordinator->render(left, right, events);
    for (int block = 0; block < 12; ++block) coordinator->render(left, right);
    auditRealtimeMemory = false;

    if (realtimeAllocations != 0 || realtimeFrees != 0) {
        std::cerr << "realtime allocation test failed: " << realtimeAllocations << " allocations, " << realtimeFrees << " frees\n";
        return false;
    }

    // The D8 worst case the architecture names: sixteen voices with `unisonVoices` 7 and drift on
    // every unison-capable source slot, published while the voices are sounding. The copy count is
    // a compiled patch value, so it crosses to the audio thread through the ordinary FIFO and the
    // per-copy banks are already sized; nothing may allocate, and polyphony stays sixteen.
    auto worstCase = compiled.patch;
    for (std::uint8_t node = 0; node < worstCase.nodeCount; ++node)
        if (worstCase.nodes[node].type == domain::ModuleType::harmonic || worstCase.nodes[node].type == domain::ModuleType::fm) {
            worstCase.nodes[node].values.unisonVoices = static_cast<int>(engine::maximumUnisonVoices);
            worstCase.nodes[node].values.detuneCents = 50.0f;
            worstCase.nodes[node].values.unisonSpread = 1.0f;
            worstCase.nodes[node].values.phaseRandom = 1.0f;
            worstCase.nodes[node].values.drift = 1.0f;
            // D8 audio-rate modulation (#127): the maximal patch cables both typed inputs, so the
            // worst case also drives them at full depth. They add no state and no allocation — the
            // signal is summed into a stack pair in the voice loop — and this render proves it.
            worstCase.nodes[node].values.modInDepth = 1.0f;
        }
    for (std::uint8_t node = 0; node < worstCase.nodeCount; ++node)
        if (worstCase.nodes[node].type == domain::ModuleType::resonator) worstCase.nodes[node].values.exciteDepth = 1.0f;
    events.clear();
    for (std::uint8_t note = 60; note < 60 + engine::maximumVoices; ++note) events.push_back({0, engine::MidiEventType::noteOn, 1, note, 100, 8192});
    realtimeAllocations = realtimeFrees = 0;
    auditRealtimeMemory = true;
    (void) coordinator->publish(worstCase, {});
    for (int block = 0; block < 8; ++block) { coordinator->render(left, right); (void) coordinator->retryPending(); }
    coordinator->render(left, right, events);
    for (int block = 0; block < 12; ++block) coordinator->render(left, right);
    auditRealtimeMemory = false;

    if (realtimeAllocations != 0 || realtimeFrees != 0) {
        std::cerr << "realtime allocation test failed under worst-case unison: " << realtimeAllocations << " allocations, " << realtimeFrees << " frees\n";
        return false;
    }
    if (coordinator->activeVoiceCount() > engine::maximumVoices) {
        std::cerr << "realtime allocation test failed: unison raised the voice count to " << coordinator->activeVoiceCount() << '\n';
        return false;
    }

    // D8 filter and shaper breadth (#126): every filter slot on `ladder24` at maximum q and drive
    // with both panel shortcuts live, and every shaper slot walked through all five curves while
    // sixteen voices sound. The ladder's four one-pole states and the curve branch are plain
    // members of an already-prepared processor, so a mode or curve change is a compiled-patch value
    // crossing the ordinary FIFO and nothing may allocate on either side of it.
    auto breadth = compiled.patch;
    for (std::uint8_t node = 0; node < breadth.nodeCount; ++node) {
        if (breadth.nodes[node].type == domain::ModuleType::filter) {
            breadth.nodes[node].values.mode = 3;
            breadth.nodes[node].values.q = 8.0f;
            breadth.nodes[node].values.drive = 16.0f;
            breadth.nodes[node].values.keytrack = 1.0f;
            breadth.nodes[node].values.envAmount = -1.0f;
        }
        if (breadth.nodes[node].type == domain::ModuleType::shaper) breadth.nodes[node].values.drive = 16.0f;
    }
    events.clear();
    for (std::uint8_t note = 52; note < 52 + engine::maximumVoices; ++note) events.push_back({0, engine::MidiEventType::noteOn, 1, note, 100, 8192});
    realtimeAllocations = realtimeFrees = 0;
    auditRealtimeMemory = true;
    (void) coordinator->publish(breadth, {});
    for (int block = 0; block < 8; ++block) { coordinator->render(left, right); (void) coordinator->retryPending(); }
    coordinator->render(left, right, events);
    for (int curve = 0; curve < 5; ++curve) {
        auto stepped = breadth;
        for (std::uint8_t node = 0; node < stepped.nodeCount; ++node) {
            if (stepped.nodes[node].type == domain::ModuleType::shaper) stepped.nodes[node].values.curve = curve;
            if (stepped.nodes[node].type == domain::ModuleType::filter) stepped.nodes[node].values.mode = curve;
        }
        (void) coordinator->publish(stepped, {});
        for (int block = 0; block < 8; ++block) { coordinator->render(left, right); (void) coordinator->retryPending(); }
    }
    auditRealtimeMemory = false;

    if (realtimeAllocations != 0 || realtimeFrees != 0) {
        std::cerr << "realtime allocation test failed while walking filter modes and shaper curves: "
                  << realtimeAllocations << " allocations, " << realtimeFrees << " frees\n";
        return false;
    }
    for (float sample : left)
        if (!std::isfinite(sample) || std::abs(sample) > 0.8912511f) {
            std::cerr << "realtime allocation test failed: filter/shaper breadth left the output guard\n";
            return false;
        }
    return true;
}

bool runCoordinatorConcurrencyTests()
{
    using namespace iupac;
    engine::CompiledPatch patch;
    patch.nodeCount = 1;
    patch.edgeCount = 1;
    patch.nodes[0].type = domain::ModuleType::mixer;
    patch.nodes[0].idHash = 1;
    patch.nodes[0].values.level = 1.0f;
    patch.edges[0] = {0, 0, 1.0f, true};

    engine::PatchCoordinator coordinator;
    coordinator.prepare(48000.0, 64);
    std::atomic<bool> producerDone{false};
    std::atomic<std::uint64_t> latest{0};
    std::thread producer([&] {
        for (int request = 0; request < 1000; ++request) {
            auto desired = patch;
            desired.nodes[0].values.level = static_cast<float>(request % 100) / 100.0f;
            latest.store(coordinator.publish(desired, {}), std::memory_order_release);
            (void) coordinator.retryPending();
        }
        while (!coordinator.retryPending()) std::this_thread::yield();
        producerDone.store(true, std::memory_order_release);
    });

    std::array<float, 64> left{}, right{};
    for (int block = 0; block < 10000; ++block) {
        coordinator.render(left, right);
        if (producerDone.load(std::memory_order_acquire)
            && coordinator.status().accepted == latest.load(std::memory_order_acquire)) break;
    }
    producer.join();
    coordinator.render(left, right);
    if (coordinator.status().accepted != latest.load(std::memory_order_acquire)) {
        std::cerr << "coordinator concurrency test failed: latest generation was not accepted\n";
        return false;
    }
    return true;
}
