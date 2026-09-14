#include "iupac/domain/ProductInfo.hpp"
#include "iupac/domain/Patch.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--catalog")
    {
        for (const auto& module : iupac::domain::moduleCatalog())
            std::cout << module.id << '\n';
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--validate-patch")
    {
        std::ifstream input(argv[2]);
        const std::string json((std::istreambuf_iterator<char>(input)), {});
        const auto decoded = iupac::domain::decodePatchJson(json);
        if (!decoded) { std::cerr << decoded.error << '\n'; return 2; }
        std::cout << iupac::domain::encodePatchJson(*decoded.value, true) << '\n';
        return 0;
    }
    std::cout << "{\"product\":\"" << iupac::domain::productName()
              << "\",\"architecture\":" << iupac::domain::architectureVersion()
              << ",\"chemistryEnabled\":false}\n";
    return 0;
}
