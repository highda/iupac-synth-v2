#include "iupac/chemistry/Analysis.hpp"

#include <iostream>
#include <string>

int main()
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
    return 0;
}
