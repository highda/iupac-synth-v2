#include "iupac/chemistry/Analysis.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
    const std::string response = R"({"analysis":{"analysisVersion":1,"backend":{"opsinSha256":"d25bc08f41b8f6fcd6f35e18ab83f3b8d9218cdb003d55c5f74aaefe2e0c68ab","opsinVersion":"2.8.0","rdkitVersion":"2022.09.3"},"canonicalIsomericSmiles":"CCO","descriptors":{"aromaticAtomFraction":0.0,"elementCounts":{"Br":0,"C":2,"Cl":0,"F":0,"I":0,"N":0,"O":1,"P":0,"S":0,"other":0},"formalCharge":0,"fractionCsp3":1.0,"fusedRingAdjacencyCount":0,"hba":1,"hbd":1,"heavyAtoms":3,"logP":-0.0014,"molecularWeight":46.069,"motifCounts":{"alcohol":1,"amide":0,"amine":0,"arylHalide":0,"carbonyl":0,"ether":0,"phenol":0},"ringCount":0,"rotatableBonds":0,"tpsa":20.23},"detail":{"atoms":[{}, {}, {}],"bonds":[],"motifMatches":{}}},"protocolVersion":1,"requestId":"42","status":"ok"})";
    const auto decoded = iupac::chemistry::decodeAnalysisResponse(response, "42");
    if (!decoded || decoded.value->canonicalIsomericSmiles != "CCO" || decoded.value->heavyAtoms != 3) { std::cerr << decoded.error << '\n'; return 1; }
    if (iupac::chemistry::decodeAnalysisResponse(response, "stale")) { std::cerr << "accepted stale request\n"; return 1; }
    auto numericResponse = response;
    const auto marker = numericResponse.find("\"requestId\":\"42\"");
    numericResponse.replace(marker, std::string("\"requestId\":\"42\"").size(), "\"requestId\":42");
    if (!iupac::chemistry::decodeAnalysisResponse(numericResponse, "42")) { std::cerr << "rejected integer request ID\n"; return 1; }
    if (iupac::chemistry::decodeAnalysisResponse(std::string(iupac::chemistry::maximumResponseBytes + 1, 'x'), "42")) return 1;

    if (argc == 2)
    {
        std::ifstream stream(argv[1], std::ios::binary);
        std::ostringstream contents;
        contents << stream.rdbuf();
        const auto fixture = juce::JSON::parse(contents.str());
        const auto* root = fixture.getDynamicObject();
        const auto* records = root != nullptr ? root->getProperty("records").getArray() : nullptr;
        if (!stream || records == nullptr || records->isEmpty()) { std::cerr << "invalid Analysis fixture\n"; return 1; }
        for (const auto& item : *records)
        {
            const auto* record = item.getDynamicObject();
            const auto id = record != nullptr ? record->getProperty("id").toString() : juce::String{};
            const auto encoded = juce::JSON::toString(record->getProperty("response"), true).toStdString();
            const auto panelDecoded = iupac::chemistry::decodeAnalysisResponse(encoded, id.toStdString());
            if (!panelDecoded) { std::cerr << "fixture " << id << ": " << panelDecoded.error << '\n'; return 1; }
        }
        const auto* invalid = root->getProperty("invalidResponses").getArray();
        if (invalid == nullptr || invalid->isEmpty()) { std::cerr << "missing invalid Analysis fixtures\n"; return 1; }
        for (const auto& item : *invalid)
        {
            const auto* record = item.getDynamicObject();
            const auto id = record != nullptr ? record->getProperty("id").toString() : juce::String{};
            const auto encoded = juce::JSON::toString(record->getProperty("response"), true).toStdString();
            if (iupac::chemistry::decodeAnalysisResponse(encoded, id.toStdString())) { std::cerr << "accepted invalid fixture " << id << '\n'; return 1; }
        }
    }
    return 0;
}
