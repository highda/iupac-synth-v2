#include "iupac/domain/ProductInfo.hpp"

#include <iostream>

int main()
{
    std::cout << "{\"product\":\"" << iupac::domain::productName()
              << "\",\"architecture\":" << iupac::domain::architectureVersion()
              << ",\"chemistryEnabled\":false}\n";
    return 0;
}
