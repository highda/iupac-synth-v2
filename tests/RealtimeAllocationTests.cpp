#include "iupac/engine/PatchCoordinator.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

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
    domain::Patch patch;
    const auto* harmonic = domain::findModule("harmonic");
    domain::Node node{"osc", harmonic->type, {}};
    for (const auto& parameter : harmonic->parameters)
        node.parameters.push_back({std::string(parameter.id), std::vector<double>(parameter.arraySize ? parameter.arraySize : 1, parameter.defaultValue)});
    node.parameters[0].values[0] = 1.0;
    for (std::size_t i = 0; i < 16; ++i) node.parameters[1].values[i] = static_cast<double>(i + 1);
    patch.nodes.push_back(std::move(node));
    patch.edges.push_back({"osc", "output", 1.0});
    const auto compiled = engine::compilePatch(patch);

    engine::PatchCoordinator coordinator;
    coordinator.prepare(48000.0, 128);
    (void) coordinator.publish(compiled.patch, {});
    std::array<float, 128> left{}, right{};
    std::array events{engine::MidiEvent{0, engine::MidiEventType::noteOn, 1, 60, 100, 8192}};

    realtimeAllocations = realtimeFrees = 0;
    auditRealtimeMemory = true;
    coordinator.render(left, right, events);
    for (int block = 0; block < 12; ++block) coordinator.render(left, right);
    auditRealtimeMemory = false;

    if (realtimeAllocations != 0 || realtimeFrees != 0) {
        std::cerr << "realtime allocation test failed: " << realtimeAllocations << " allocations, " << realtimeFrees << " frees\n";
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
