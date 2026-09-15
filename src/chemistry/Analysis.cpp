#include "iupac/chemistry/Analysis.hpp"

#include <cmath>
#include <set>

namespace iupac::chemistry
{
namespace
{
const juce::DynamicObject* object(const juce::var& value) { return value.getDynamicObject(); }
const juce::Array<juce::var>* array(const juce::var& value) { return value.getArray(); }
bool keys(const juce::DynamicObject& value, std::initializer_list<const char*> exact)
{
    std::set<std::string> expected;
    for (auto* name : exact) { expected.emplace(name); if (!value.hasProperty(name)) return false; }
    for (const auto& property : value.getProperties())
        if (!expected.contains(property.name.toString().toStdString())) return false;
    return true;
}
bool integer(const juce::var& value, std::int64_t& output)
{
    if (!(value.isInt() || value.isInt64())) return false;
    output = static_cast<std::int64_t>(static_cast<juce::int64>(value));
    return true;
}
bool number(const juce::var& value)
{
    return (value.isInt() || value.isInt64() || value.isDouble()) && std::isfinite(static_cast<double>(value));
}
std::string text(const juce::var& value) { return value.isString() ? value.toString().toStdString() : std::string{}; }
std::string requestIdText(const juce::var& value)
{
    if (value.isString() || value.isInt() || value.isInt64()) return value.toString().toStdString();
    return {};
}
AnalysisResult fail(std::string error) { return {{}, std::move(error)}; }
}

AnalysisResult decodeAnalysisResponse(std::string_view json, std::string_view expectedRequestId)
{
    if (json.empty() || json.size() > maximumResponseBytes) return fail("helper response is empty or exceeds 256 KiB");
    if (!juce::CharPointer_UTF8::isValidString(json.data(), static_cast<int>(json.size()))) return fail("helper response is not UTF-8");
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(json.data(), static_cast<int>(json.size())));
    const auto* root = object(parsed);
    if (root == nullptr || !keys(*root, {"analysis", "protocolVersion", "requestId", "status"})) return fail("helper success response has an invalid shape");
    std::int64_t protocol{};
    if (!integer(root->getProperty("protocolVersion"), protocol) || protocol != protocolVersion) return fail("unsupported helper protocol version");
    if (requestIdText(root->getProperty("requestId")) != expectedRequestId || text(root->getProperty("status")) != "ok") return fail("helper response request/status mismatch");
    const auto* analysis = object(root->getProperty("analysis"));
    if (analysis == nullptr || !keys(*analysis, {"analysisVersion", "backend", "canonicalIsomericSmiles", "descriptors", "detail"})) return fail("Analysis v1 has an invalid shape");
    std::int64_t version{};
    if (!integer(analysis->getProperty("analysisVersion"), version) || version != analysisVersion) return fail("unsupported Analysis version");
    const auto* backend = object(analysis->getProperty("backend"));
    if (backend == nullptr || !keys(*backend, {"opsinSha256", "opsinVersion", "rdkitVersion"})) return fail("Analysis backend identity is invalid");
    Analysis output;
    output.canonicalIsomericSmiles = text(analysis->getProperty("canonicalIsomericSmiles"));
    output.rdkitVersion = text(backend->getProperty("rdkitVersion"));
    output.opsinVersion = text(backend->getProperty("opsinVersion"));
    if (output.canonicalIsomericSmiles.empty() || output.rdkitVersion.empty() || output.opsinVersion != "2.8.0"
        || text(backend->getProperty("opsinSha256")) != "d25bc08f41b8f6fcd6f35e18ab83f3b8d9218cdb003d55c5f74aaefe2e0c68ab") return fail("Analysis backend identity is unsupported");
    const auto descriptorValue = analysis->getProperty("descriptors");
    const auto* descriptors = object(descriptorValue);
    if (descriptors == nullptr || !keys(*descriptors, {"aromaticAtomFraction", "elementCounts", "formalCharge", "fractionCsp3", "fusedRingAdjacencyCount", "hba", "hbd", "heavyAtoms", "logP", "molecularWeight", "motifCounts", "ringCount", "rotatableBonds", "tpsa"})) return fail("Analysis descriptors have an invalid shape");
    constexpr std::array numericNames{"aromaticAtomFraction", "fractionCsp3", "logP", "molecularWeight", "tpsa"};
    for (auto* name : numericNames) if (!number(descriptors->getProperty(name))) return fail("Analysis descriptor is not finite");
    std::int64_t heavy{}, charge{};
    if (!integer(descriptors->getProperty("heavyAtoms"), heavy) || heavy < 1 || heavy > 256
        || !integer(descriptors->getProperty("formalCharge"), charge)) return fail("Analysis atom count or charge is invalid");
    output.heavyAtoms = static_cast<std::size_t>(heavy); output.formalCharge = static_cast<int>(charge);
    const auto* counts = object(descriptors->getProperty("elementCounts"));
    constexpr std::array countNames{"C", "N", "O", "S", "P", "F", "Cl", "Br", "I", "other"};
    if (counts == nullptr || !keys(*counts, {"C", "N", "O", "S", "P", "F", "Cl", "Br", "I", "other"})) return fail("Analysis element counts are invalid");
    std::size_t countSum = 0;
    for (std::size_t i = 0; i < countNames.size(); ++i) { std::int64_t count{}; if (!integer(counts->getProperty(countNames[i]), count) || count < 0) return fail("Analysis element count is invalid"); output.elementCounts[i] = static_cast<std::size_t>(count); countSum += output.elementCounts[i]; }
    if (countSum != output.heavyAtoms) return fail("Analysis element counts do not match heavy atoms");
    const auto detailValue = analysis->getProperty("detail"); const auto* detail = object(detailValue);
    if (detail == nullptr || !keys(*detail, {"atoms", "bonds", "motifMatches"})) return fail("Analysis detail has an invalid shape");
    const auto* atoms = array(detail->getProperty("atoms")); const auto* bonds = array(detail->getProperty("bonds"));
    if (atoms == nullptr || atoms->size() < static_cast<int>(output.heavyAtoms) || atoms->size() > 512 || bonds == nullptr || bonds->size() > 1024) return fail("Analysis detail exceeds structural bounds");
    output.descriptors = descriptorValue.clone(); output.detail = detailValue.clone();
    return {std::move(output), {}};
}
}
