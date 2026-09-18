#include "iupac/chemistry/Extension.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace iupac::chemistry
{
namespace
{
constexpr std::size_t maximumInputBytes = 4096;
constexpr std::size_t maximumDiagnosticBytes = 64 * 1024;

std::string modeName(InputMode mode) { return mode == InputMode::name ? "name" : "smiles"; }
std::string validateInput(std::string_view text)
{
    if (text.empty()) return "molecular input is empty";
    if (text.size() > maximumInputBytes) return "molecular input exceeds 4096 UTF-8 bytes";
    if (text.find('\0') != std::string_view::npos || text.find('\n') != std::string_view::npos || text.find('\r') != std::string_view::npos)
        return "molecular input contains a forbidden NUL or newline";
    if (!juce::CharPointer_UTF8::isValidString(text.data(), static_cast<int>(text.size()))) return "molecular input is not UTF-8";
    return {};
}
juce::var requestValue(InputMode mode, std::string_view text, std::string_view id)
{
    auto value = juce::var(new juce::DynamicObject);
    auto* object = value.getDynamicObject();
    object->setProperty("protocolVersion", protocolVersion);
    object->setProperty("requestId", juce::String::fromUTF8(id.data(), static_cast<int>(id.size())));
    object->setProperty("mode", juce::String(modeName(mode)));
    object->setProperty("text", juce::String::fromUTF8(text.data(), static_cast<int>(text.size())));
    return value;
}
std::string responseError(std::string_view text)
{
    if (text.empty()) return "helper produced no response";
    const auto value = juce::JSON::parse(juce::String::fromUTF8(text.data(), static_cast<int>(text.size())));
    if (const auto* object = value.getDynamicObject()) {
        auto diagnostic = object->getProperty("diagnostic").toString().toStdString();
        if (diagnostic.empty()) diagnostic = object->getProperty("error").toString().toStdString();
        if (!diagnostic.empty()) return diagnostic;
    }
    return "helper returned an invalid or unsuccessful response";
}
void closeFd(int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } }
void appendBounded(std::string& destination, const char* bytes, std::size_t count, std::size_t limit)
{
    const auto available = limit > destination.size() ? limit - destination.size() : 0;
    destination.append(bytes, std::min(available, count));
}
}

HelperConfiguration locatePackagedHelper(const std::filesystem::path& supplied)
{
    if (const auto executable = juce::SystemStats::getEnvironmentVariable("IUPAC_CHEMISTRY_HELPER", {}); executable.isNotEmpty()) {
        std::filesystem::path path{executable.toStdString()};
        return {path, path.parent_path().parent_path(), std::chrono::milliseconds{15000}};
    }
    std::vector<std::filesystem::path> roots;
    if (!supplied.empty()) roots.push_back(supplied);
    if (const auto configured = juce::SystemStats::getEnvironmentVariable("IUPAC_CHEMISTRY_ROOT", {}); configured.isNotEmpty())
        roots.emplace_back(configured.toStdString());
    roots.emplace_back(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getFullPathName().toStdString());
    for (auto root : roots) {
        for (int up = 0; up < 4; ++up) {
            for (const auto& relative : {std::filesystem::path{"helper/iupac-analysis-helper"}, std::filesystem::path{"Resources/chemistry/helper/iupac-analysis-helper"}, std::filesystem::path{"resources/chemistry/helper/iupac-analysis-helper"}}) {
                auto executable = root / relative;
                if (std::filesystem::is_regular_file(executable)) return {executable, executable.parent_path().parent_path(), std::chrono::milliseconds{15000}};
            }
            root = root.parent_path();
        }
    }
    return {};
}

ProtocolReply invokeProtocol(const HelperConfiguration& configuration, const juce::var& requestValue,
                             std::stop_token stop)
{
    ProtocolReply result;
    if (configuration.executable.empty() || !std::filesystem::is_regular_file(configuration.executable)) {
        result.error = "chemistry helper is missing or damaged; repair or reinstall the product"; return result;
    }
    int stdinPipe[2]{-1,-1}, stdoutPipe[2]{-1,-1}, stderrPipe[2]{-1,-1};
    if (::pipe(stdinPipe) || ::pipe(stdoutPipe) || ::pipe(stderrPipe)) {
        for (auto* pair : {stdinPipe, stdoutPipe, stderrPipe}) { closeFd(pair[0]); closeFd(pair[1]); }
        result.error = "could not create private helper pipes"; return result;
    }
#if defined(__APPLE__)
    (void)::fcntl(stdinPipe[1], F_SETNOSIGPIPE, 1);
#endif
    const auto executable = configuration.executable.string();
    std::vector<std::string> environmentStorage; std::vector<char*> environment;
    const auto blocked = [](std::string_view entry) {
        constexpr std::array names{"PYTHONHOME=","PYTHONPATH=","VIRTUAL_ENV=","CONDA_PREFIX=","JAVA_HOME=","JDK_HOME=","LD_LIBRARY_PATH=","DYLD_LIBRARY_PATH=","DYLD_FALLBACK_LIBRARY_PATH=","DYLD_INSERT_LIBRARIES="};
        return std::ranges::any_of(names, [&](std::string_view name){ return entry.starts_with(name); });
    };
    for (auto item = environ; item != nullptr && *item != nullptr; ++item) if (!blocked(*item)) environmentStorage.emplace_back(*item);
    for (auto& item : environmentStorage) environment.push_back(item.data()); environment.push_back(nullptr);
    std::array<char*,2> arguments{const_cast<char*>(executable.c_str()), nullptr};
    posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdinPipe[0], STDIN_FILENO); posix_spawn_file_actions_adddup2(&actions, stdoutPipe[1], STDOUT_FILENO); posix_spawn_file_actions_adddup2(&actions, stderrPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdinPipe[1]); posix_spawn_file_actions_addclose(&actions, stdoutPipe[0]); posix_spawn_file_actions_addclose(&actions, stderrPipe[0]);
    posix_spawnattr_t attributes; posix_spawnattr_init(&attributes); posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&attributes, 0);
    pid_t pid{}; const auto spawnError = ::posix_spawn(&pid, executable.c_str(), &actions, &attributes, arguments.data(), environment.data());
    posix_spawnattr_destroy(&attributes); posix_spawn_file_actions_destroy(&actions);
    closeFd(stdinPipe[0]); closeFd(stdoutPipe[1]); closeFd(stderrPipe[1]);
    if (spawnError != 0) { closeFd(stdinPipe[1]); closeFd(stdoutPipe[0]); closeFd(stderrPipe[0]); result.error = "could not start chemistry helper"; return result; }
    const auto request = juce::JSON::toString(requestValue, false).toStdString();
    if (request.size() > domain::maximumDocumentBytes) { result.error = "helper request exceeds 1 MiB"; ::kill(-pid, SIGKILL); closeFd(stdinPipe[1]); closeFd(stdoutPipe[0]); closeFd(stderrPipe[0]); (void)::waitpid(pid, nullptr, 0); return result; }
    sigset_t blockedSignals{}, previousSignals{}; sigemptyset(&blockedSignals); sigaddset(&blockedSignals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blockedSignals, &previousSignals);
    std::size_t written = 0; bool brokenInput = false;
    while (written < request.size()) { const auto count = ::write(stdinPipe[1], request.data() + written, request.size() - written); if (count <= 0) { brokenInput = true; break; } written += static_cast<std::size_t>(count); }
#if defined(__APPLE__)
    // Darwin has no sigtimedwait to drain a pending SIGPIPE; the descriptor was opened with F_SETNOSIGPIPE instead.
#else
    if (brokenInput) { timespec now{}; (void)::sigtimedwait(&blockedSignals, nullptr, &now); }
#endif
    pthread_sigmask(SIG_SETMASK, &previousSignals, nullptr);
    closeFd(stdinPipe[1]);
    const auto deadline = std::chrono::steady_clock::now() + configuration.deadline;
    std::string output, diagnostic; bool stdoutOpen = true, stderrOpen = true, killed = false;
    while (stdoutOpen || stderrOpen) {
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) { ::kill(-pid, SIGKILL); killed = true; break; }
        pollfd descriptors[2]{{stdoutPipe[0], POLLIN, 0},{stderrPipe[0], POLLIN, 0}};
        const int ready = ::poll(descriptors, 2, 25);
        if (ready < 0 && errno != EINTR) break;
        auto readOne = [](int& fd, bool& open, std::string& destination, std::size_t limit) {
            std::array<char,4096> buffer{}; const auto count = ::read(fd, buffer.data(), buffer.size());
            if (count > 0) appendBounded(destination, buffer.data(), static_cast<std::size_t>(count), limit);
            else if (count == 0) { closeFd(fd); open = false; }
        };
        if (stdoutOpen && descriptors[0].revents) readOne(stdoutPipe[0], stdoutOpen, output, maximumResponseBytes + 1);
        if (stderrOpen && descriptors[1].revents) readOne(stderrPipe[0], stderrOpen, diagnostic, maximumDiagnosticBytes);
    }
    closeFd(stdoutPipe[0]); closeFd(stderrPipe[0]);
    int status{};
    while (::waitpid(pid, &status, WNOHANG) == 0) {
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) { ::kill(-pid, SIGKILL); killed = true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (stop.stop_requested()) { result.error = "chemistry request cancelled"; return result; }
    if (killed) { result.error = "chemistry helper exceeded its deadline"; return result; }
    if (output.size() > maximumResponseBytes) { result.error = "helper response exceeds 256 KiB"; return result; }
    result.responseJson = std::move(output);
    result.response = juce::JSON::parse(juce::String::fromUTF8(result.responseJson.data(), static_cast<int>(result.responseJson.size())));
    const auto* object = result.response.getDynamicObject();
    if (!object || object->getProperty("status").toString() != "ok") { result.error = responseError(result.responseJson); if (!diagnostic.empty()) result.error += ": " + diagnostic; }
    return result;
}

HelperReply invokeHelper(const HelperConfiguration& configuration, InputMode mode, std::string_view input,
                         std::string requestId, std::stop_token stop)
{
    HelperReply result;
    if (auto error = validateInput(input); !error.empty()) { result.error = std::move(error); return result; }
    auto protocol = invokeProtocol(configuration, requestValue(mode, input, requestId), stop);
    if (!protocol) { result.error = std::move(protocol.error); return result; }
    auto decoded = decodeAnalysisResponse(protocol.responseJson, requestId);
    if (!decoded) { result.error = decoded.error; return result; }
    result.analysis = std::move(decoded.value); result.response = std::move(protocol.response);
    result.responseJson = std::move(protocol.responseJson); return result;
}

juce::var makeProvenance(const ApplyResult& value)
{
    auto root = juce::var(new juce::DynamicObject); auto* object = root.getDynamicObject();
    object->setProperty("kind", "chemistry-generation-v1"); object->setProperty("mode", juce::String(modeName(value.mode)));
    object->setProperty("input", juce::String(value.input)); object->setProperty("analysis", value.helperResponse.clone());
    object->setProperty("mappingTrace", value.trace.clone()); return root;
}

ExtensionCoordinator::ExtensionCoordinator(HelperConfiguration configuration, Completion completion)
    : configuration_(std::move(configuration)), completion_(std::move(completion)), worker_([this](std::stop_token stop){ run(stop); }) {}
ExtensionCoordinator::~ExtensionCoordinator() { cancel(); worker_.request_stop(); wake_.notify_all(); }
std::uint64_t ExtensionCoordinator::apply(InputMode mode, std::string input)
{
    std::uint64_t generation{};
    { std::scoped_lock lock(mutex_); generation = generation_.fetch_add(1) + 1; pending_ = Request{generation, mode, std::move(input)}; }
    wake_.notify_all(); return generation;
}
void ExtensionCoordinator::cancel() { { std::scoped_lock lock(mutex_); generation_.fetch_add(1); pending_.reset(); } wake_.notify_all(); }
void ExtensionCoordinator::run(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        std::optional<Request> request;
        { std::unique_lock lock(mutex_); wake_.wait(lock, stop, [this]{ return pending_.has_value(); }); if (stop.stop_requested()) return; request = std::move(pending_); pending_.reset(); }
        std::stop_source operationStop; std::stop_callback stopWorker(stop, [&]{ operationStop.request_stop(); });
        std::jthread cancellationWatcher([this, expected=request->generation, &operationStop](std::stop_token watcherStop){
            while (!watcherStop.stop_requested() && generation_.load() == expected) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!watcherStop.stop_requested()) operationStop.request_stop();
        });
        ApplyResult result; result.generation=request->generation; result.mode=request->mode; result.input=request->input;
        auto reply=invokeHelper(configuration_,request->mode,request->input,std::to_string(request->generation),operationStop.get_token());
        cancellationWatcher.request_stop();
        if (reply) { auto generated=generate(*reply.analysis); result.analysis=reply.analysis; result.helperResponse=reply.response;
            if (generated) { result.intent=generated.intent; result.patch=generated.patch; result.trace=generated.trace; } else result.error=generated.error;
        } else result.error=reply.error;
        if (generation_.load() == request->generation && completion_) completion_(std::move(result));
    }
}
}
