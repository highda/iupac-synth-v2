#pragma once

#include "iupac/chemistry/Analysis.hpp"
#include "iupac/chemistry/Mapping.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace iupac::chemistry
{
enum class InputMode { name, smiles };

struct HelperReply
{
    std::optional<Analysis> analysis;
    juce::var response;
    std::string responseJson;
    std::string error;
    explicit operator bool() const noexcept { return analysis.has_value(); }
};

struct ProtocolReply
{
    juce::var response;
    std::string responseJson;
    std::string error;
    explicit operator bool() const noexcept { return error.empty() && response.getDynamicObject() != nullptr; }
};

struct ApplyResult
{
    std::uint64_t generation{};
    InputMode mode{InputMode::smiles};
    std::string input;
    std::optional<Analysis> analysis;
    std::optional<SonicIntent> intent;
    std::optional<domain::Patch> patch;
    juce::var trace;
    juce::var helperResponse;
    std::string error;
    explicit operator bool() const noexcept { return patch.has_value(); }
};

struct HelperConfiguration
{
    std::filesystem::path executable;
    std::filesystem::path resourceRoot;
    // 45 s, not 15 s: measured first-use cost of a freshly installed private payload
    // on macOS arm64 (#42, D4 budget). The first execution of the frozen helper after
    // its files are written takes 15.8 s while the system validates the ~700 Mach-O
    // files of the RDKit closure, and the first OPSIN run 7.9 s; steady state is 0.2 s
    // and 5.3 s. CHEMISTRY.md records the same value.
    std::chrono::milliseconds deadline{45000};
};

[[nodiscard]] HelperConfiguration locatePackagedHelper(const std::filesystem::path& anchor = {});
[[nodiscard]] ProtocolReply invokeProtocol(const HelperConfiguration&, const juce::var& request,
                                           std::stop_token = {});
[[nodiscard]] HelperReply invokeHelper(const HelperConfiguration&, InputMode, std::string_view,
                                       std::string requestId, std::stop_token = {});
[[nodiscard]] juce::var makeProvenance(const ApplyResult&);

// One active worker and one replaceable latest request. Destruction cancels and
// reaps the complete helper process group before returning.
class ExtensionCoordinator
{
public:
    using Completion = std::function<void(ApplyResult)>;
    explicit ExtensionCoordinator(HelperConfiguration, Completion);
    ~ExtensionCoordinator();
    ExtensionCoordinator(const ExtensionCoordinator&) = delete;
    ExtensionCoordinator& operator=(const ExtensionCoordinator&) = delete;
    std::uint64_t apply(InputMode, std::string);
    void cancel();
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_.load(); }

private:
    struct Request { std::uint64_t generation; InputMode mode; std::string input; };
    void run(std::stop_token);
    HelperConfiguration configuration_;
    Completion completion_;
    std::atomic<std::uint64_t> generation_{0};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Request> pending_;
    std::jthread worker_;
};
}
