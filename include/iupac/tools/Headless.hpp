#pragma once

#include "iupac/domain/Patch.hpp"
#include "iupac/engine/Engine.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iupac::tools
{
inline constexpr std::size_t maximumInputBytes = 1024 * 1024;
inline constexpr std::size_t maximumPanelCases = 128;
struct ReadResult { std::optional<std::string> value; std::string error; explicit operator bool() const { return value.has_value(); } };
struct MidiResult { std::vector<engine::MidiEvent> events; std::string error; explicit operator bool() const { return error.empty(); } };
// `tempoBpm` is the tempo the tail's synced delay renders at. A CLI render has no host playhead, so
// it is the documented fallback unless a panel case states one, and the manifest records both the
// value and whether it came from a playhead (ARCHITECTURE "Tempo sync").
struct RenderSettings { double sampleRate{48000.0}; std::size_t blockSize{128}; std::size_t samples{}; double tempoBpm{engine::fallbackTempoBpm}; bool tempoFromPlayhead{false}; };
struct RenderResult { std::string manifestJson; std::string error; explicit operator bool() const { return error.empty(); } };

[[nodiscard]] ReadResult readBoundedFile(const std::filesystem::path&);
[[nodiscard]] MidiResult decodeMidiJson(std::string_view, std::size_t maximumSamples);
[[nodiscard]] std::string inspectCatalog();
[[nodiscard]] std::string inspectPatch(const domain::Patch&, bool compiled);
[[nodiscard]] std::string inspectEffective(const domain::State&);
[[nodiscard]] std::string graphSignature(const engine::CompiledPatch&);
[[nodiscard]] std::string valueSignature(const engine::CompiledPatch&, const domain::HostControls&);
[[nodiscard]] RenderResult renderSnapshot(const domain::State&, std::span<const engine::MidiEvent>, const RenderSettings&, const std::filesystem::path&);
int verifyPanel(const std::filesystem::path&, const std::filesystem::path&, std::string& report, std::string& error);
[[nodiscard]] std::string benchmark(const domain::State&, std::size_t seconds, std::string& error);
}
