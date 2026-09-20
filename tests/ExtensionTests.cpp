#include "iupac/chemistry/Extension.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

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
    return ok ? 0 : 1;
}
