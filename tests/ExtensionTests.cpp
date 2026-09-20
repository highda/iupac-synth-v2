#include "iupac/chemistry/Extension.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <vector>

namespace { bool expect(bool condition, const char* message) { if (!condition) std::cerr << "extension test failed: " << message << '\n'; return condition; } }

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    using namespace std::chrono_literals;
    using namespace iupac::chemistry;
    bool ok = true;
    HelperConfiguration configuration{argv[1], {}, 2s};
    auto direct = invokeHelper(configuration, InputMode::smiles, "CCO", "direct");
    ok &= expect(direct && direct.analysis->canonicalIsomericSmiles == "CCO", "production supervisor decodes a successful response");
    auto rejected = invokeHelper(configuration, InputMode::smiles, "reject", "bad");
    ok &= expect(!rejected && rejected.error.find("unsupported fixture") != std::string::npos, "helper diagnostic is retained and current sound can be preserved");
    ok &= expect(!invokeHelper(configuration, InputMode::smiles, std::string(4097, 'C'), "large"), "oversized input is rejected before spawn");
    HelperConfiguration missing{"/definitely/missing/iupac-helper", {}, 50ms};
    ok &= expect(invokeHelper(missing, InputMode::smiles, "CCO", "missing").error.find("repair or reinstall") != std::string::npos, "corrupt install has a distinct recovery diagnostic");
    auto shortDeadline=configuration;shortDeadline.deadline=100ms;const auto deadlineStart=std::chrono::steady_clock::now();auto hung=invokeHelper(shortDeadline,InputMode::smiles,"closed-stream-hang","hung");ok&=expect(!hung&&hung.error.find("deadline")!=std::string::npos&&std::chrono::steady_clock::now()-deadlineStart<1s,"closed output cannot bypass deadline and tree cleanup");

#if defined(__APPLE__)
    {   // #42: the macOS bundle probe resolves the payload from the bundle itself,
        // not from a working directory, and only for a real bundle.
        const auto enclosing = std::filesystem::temp_directory_path() / "iupac bundle probe";
        const auto bundle = enclosing / "IUPAC Synth 2.vst3";
        const auto expected = bundle / "Contents/Resources/chemistry/helper/iupac-analysis-helper";
        std::filesystem::remove_all(enclosing);
        std::filesystem::create_directories(expected.parent_path());
        std::ofstream(bundle / "Contents/Info.plist") << "<plist version=\"1.0\"><dict/></plist>\n";
        std::filesystem::copy_file(argv[1], expected);
        const auto located = locatePackagedHelper(bundle);
        ok &= expect(located.executable == expected, "bundle probe finds the payload in the bundle's resources");
        auto fromBundle = invokeHelper({located.executable, located.resourceRoot, 2s}, InputMode::smiles, "CCO", "bundle");
        ok &= expect(fromBundle && fromBundle.analysis->canonicalIsomericSmiles == "CCO", "the payload found in a bundle answers");
        std::filesystem::remove(bundle / "Contents/Info.plist");
        ok &= expect(locatePackagedHelper(bundle).executable.empty(), "a directory without Info.plist is not treated as a bundle");
        std::filesystem::remove_all(enclosing);
    }
#endif

    std::mutex mutex; std::condition_variable wake; std::optional<ApplyResult> completed;
    {
        ExtensionCoordinator coordinator(configuration, [&](ApplyResult result) { std::scoped_lock lock(mutex); completed = std::move(result); wake.notify_all(); });
        const auto stale = coordinator.apply(InputMode::smiles, "slow");
        const auto latest = coordinator.apply(InputMode::smiles, "CCO");
        ok &= expect(latest > stale, "request generations are monotonic");
        std::unique_lock lock(mutex); wake.wait_for(lock, 3s, [&]{ return completed.has_value(); });
        ok &= expect(completed && completed->generation == latest && *completed, "storm cancels stale work and publishes latest only");
    }
    const auto start = std::chrono::steady_clock::now();
    { ExtensionCoordinator coordinator(configuration, [](ApplyResult){}); coordinator.apply(InputMode::smiles, "slow"); }
    ok &= expect(std::chrono::steady_clock::now() - start < 1s, "teardown cancels and reaps an active helper tree");

    // #97 pre-warm. The payload's first execution costs 13-30 s on a fresh macOS install
    // (docs/chemistry-cold-start.md); prewarm() pays it on the worker before the user asks
    // for anything, and never blocks the caller.
    {
        std::mutex stageMutex; std::vector<Stage> stages;
        ExtensionCoordinator coordinator(configuration, [](ApplyResult){},
            [&](Stage stage, std::uint64_t){ std::scoped_lock lock(stageMutex); stages.push_back(stage); });
        const auto requested = std::chrono::steady_clock::now();
        coordinator.prewarm();
        ok &= expect(std::chrono::steady_clock::now() - requested < 50ms, "prewarm returns to its caller without waiting for the helper");
        for (int i = 0; i < 300 && !coordinator.warmed(); ++i) std::this_thread::sleep_for(10ms);
        ok &= expect(coordinator.warmed(), "prewarm runs the warm protocol action against the helper");
        coordinator.prewarm();
        std::this_thread::sleep_for(100ms);
        std::scoped_lock lock(stageMutex);
        ok &= expect(std::count(stages.begin(), stages.end(), Stage::warming) == 1, "a warmed coordinator does not warm again");
        ok &= expect(!stages.empty() && stages.front() == Stage::warming && stages.back() == Stage::idle, "the warm-up reports a warming stage and returns to idle");
        ok &= expect(coordinator.stage() == Stage::idle, "no stage is left in flight after a warm-up");
    }
    {   // An analysis asked for during a warm-up still completes, and the stages it reports
        // are the real ones, in order.
        std::mutex mutex; std::condition_variable wake; std::optional<ApplyResult> completed;
        std::mutex stageMutex; std::vector<Stage> stages;
        ExtensionCoordinator coordinator(configuration, [&](ApplyResult result){ std::scoped_lock lock(mutex); completed = std::move(result); wake.notify_all(); },
            [&](Stage stage, std::uint64_t){ std::scoped_lock lock(stageMutex); stages.push_back(stage); });
        coordinator.prewarm();
        const auto generation = coordinator.apply(InputMode::smiles, "CCO");
        std::unique_lock lock(mutex); wake.wait_for(lock, 5s, [&]{ return completed.has_value(); });
        ok &= expect(completed && completed->generation == generation && *completed, "an apply during a warm-up still publishes its own result");
        std::scoped_lock stageLock(stageMutex);
        const auto analysing = std::find(stages.begin(), stages.end(), Stage::analysing);
        const auto generating = std::find(stages.begin(), stages.end(), Stage::generating);
        ok &= expect(analysing != stages.end() && generating != stages.end() && analysing < generating, "analysing is reported before generating");
    }
    {   // Cancel must reach a warm-up too, or the popup's Cancel button would be a lie
        // during the one wait it exists for.
        ExtensionCoordinator coordinator(configuration, [](ApplyResult){});
        coordinator.prewarm();
        std::this_thread::sleep_for(50ms);
        const auto cancelled = std::chrono::steady_clock::now();
        coordinator.cancel();
        for (int i = 0; i < 200 && coordinator.stage() != Stage::idle; ++i) std::this_thread::sleep_for(10ms);
        ok &= expect(coordinator.stage() == Stage::idle && std::chrono::steady_clock::now() - cancelled < 2s, "cancel ends a running warm-up");
    }
    ok &= expect(stageDescription(Stage::idle) == "Ready" && !stageDescription(Stage::warming).empty(), "every stage has a description the popup can show");
    return ok ? 0 : 1;
}
