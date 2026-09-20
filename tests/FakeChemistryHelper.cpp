#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

int main()
{
    const std::string request((std::istreambuf_iterator<char>(std::cin)), {});
    const auto marker = request.find("\"requestId\":");
    const auto begin = request.find('"', marker + 12), end = request.find('"', begin + 1);
    const auto requestId = request.substr(begin + 1, end - begin - 1);
    if (request.find("closed-stream-hang") != std::string::npos) { std::fclose(stdout); std::fclose(stderr); std::this_thread::sleep_for(std::chrono::seconds(5)); return 0; }
    // #97 pre-warm: answer the warm action the coordinator sends before any user input,
    // after an optional delay that stands in for a real payload's first-execution cost.
    if (request.find("\"action\":\"warm\"") != std::string::npos) {
        if (const auto* delay = std::getenv("IUPAC_FAKE_WARM_DELAY_MS"))
            std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(delay)));
        std::cout << "{\"protocolVersion\":1,\"requestId\":\"" << requestId
                  << "\",\"status\":\"ok\",\"warm\":{\"discovery\":\"ok\",\"opsin\":\"ok\"}}";
        return 0;
    }
    // #103: a bounded offline search returns far more candidates than a popup menu shows. Answer `discover`
    // with 50 synthetic records so the editor test drives the production search/populate path and can assert
    // that the candidate list scrolls to reach every one of them.
    // The protocol is written with juce::JSON::toString(v, false), so match the value, not a packed "key":"value".
    if (request.find("\"discover\"") != std::string::npos) {
        std::string reply = "{\"protocolVersion\":1,\"requestId\":\"" + requestId
                          + "\",\"status\":\"ok\",\"discovery\":{\"query\":\"acid\",\"candidates\":[";
        for (int i = 0; i < 50; ++i) {
            const auto n = std::to_string(i);
            if (i != 0) reply += ',';
            reply += "{\"displayName\":\"fixture acid " + n + "\",\"identity\":\"fixture-" + n
                   + "\",\"recordId\":\"Q" + std::to_string(1000 + i)
                   + "\",\"canonicalIsomericSmiles\":\"CCO\",\"validationStatus\":\"validated\""
                   + ",\"revisionUrl\":\"offline\"}";
        }
        reply += "]}}";
        std::cout << reply;
        return 0;
    }
    if (request.find("slow") != std::string::npos) std::this_thread::sleep_for(std::chrono::seconds(5));
    if (request.find("reject") != std::string::npos) {
        std::cout << "{\"diagnostic\":\"unsupported fixture\",\"protocolVersion\":1,\"requestId\":\"" << requestId << "\",\"status\":\"error\"}";
        return 2;
    }
    std::cout << R"({"analysis":{"analysisVersion":1,"backend":{"opsinSha256":"d25bc08f41b8f6fcd6f35e18ab83f3b8d9218cdb003d55c5f74aaefe2e0c68ab","opsinVersion":"2.8.0","rdkitVersion":"2022.09.3"},"canonicalIsomericSmiles":"CCO","descriptors":{"aromaticAtomFraction":0.0,"elementCounts":{"Br":0,"C":2,"Cl":0,"F":0,"I":0,"N":0,"O":1,"P":0,"S":0,"other":0},"formalCharge":0,"fractionCsp3":1.0,"fusedRingAdjacencyCount":0,"hba":1,"hbd":1,"heavyAtoms":3,"logP":-0.0014,"molecularWeight":46.069,"motifCounts":{"alcohol":1,"amide":0,"amine":0,"arylHalide":0,"carbonyl":0,"ether":0,"phenol":0},"ringCount":0,"rotatableBonds":0,"tpsa":20.23},"detail":{"atoms":[{},{},{}],"bonds":[],"motifMatches":{}}},"protocolVersion":1,"requestId":")" << requestId << R"(","status":"ok"})";
}
