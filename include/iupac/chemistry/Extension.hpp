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
    std::chrono::milliseconds deadline{15000};
};

[[nodiscard]] HelperConfiguration locatePackagedHelper(const std::filesystem::path& anchor = {});
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
