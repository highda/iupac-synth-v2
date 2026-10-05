#include "iupac/chemistry/Profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <vector>

namespace iupac::chemistry
{
namespace
{
struct Atom { int number{}, isotope{}, charge{}; bool aromatic{}; std::string stereo; std::vector<std::pair<int, double>> bonds; };
struct Bond { int a{}, b{}; double order{}; std::string stereo; };

double num(const juce::DynamicObject* o, const char* key) { return o == nullptr ? 0.0 : static_cast<double>(o->getProperty(key)); }
bool isHalogen(int z) { return z == 9 || z == 17 || z == 35 || z == 53 || z == 85; }
bool isMetalloid(int z) { return z == 5 || z == 14 || z == 32 || z == 33 || z == 34 || z == 51 || z == 52; }
bool isMetal(int z)
{
    return z == 3 || z == 4 || (z >= 11 && z <= 13) || (z >= 19 && z <= 31) || (z >= 37 && z <= 50) || (z >= 55 && z <= 84) || z >= 87;
}

// Unweighted shortest paths from one atom; -1 marks another fragment. `skip` removes one bond,
// which is how a ring bond is recognised: its two ends are still connected without it.
std::vector<int> distances(const std::vector<Atom>& atoms, int from, int skipA = -1, int skipB = -1)
{
    std::vector<int> d(atoms.size(), -1);
    std::queue<int> q; d[static_cast<std::size_t>(from)] = 0; q.push(from);
    while (!q.empty())
    {
        const int u = q.front(); q.pop();
        for (const auto& [v, order] : atoms[static_cast<std::size_t>(u)].bonds)
        {
            if ((u == skipA && v == skipB) || (u == skipB && v == skipA) || d[static_cast<std::size_t>(v)] >= 0) continue;
            d[static_cast<std::size_t>(v)] = d[static_cast<std::size_t>(u)] + 1; q.push(v);
        }
    }
    return d;
}
}

MoleculeProfile profile(const Analysis& a)
{
    MoleculeProfile p;
    const auto* d = a.descriptors.getDynamicObject();
    const auto* detail = a.detail.getDynamicObject();
    const auto* motifs = d == nullptr ? nullptr : d->getProperty("motifCounts").getDynamicObject();
    const double heavy = std::max<double>(1.0, static_cast<double>(a.heavyAtoms));

    std::vector<Atom> atoms;
    if (const auto* list = detail == nullptr ? nullptr : detail->getProperty("atoms").getArray())
        for (const auto& v : *list)
        {
            const auto* o = v.getDynamicObject();
            Atom atom; atom.number = static_cast<int>(num(o, "atomicNumber")); atom.isotope = static_cast<int>(num(o, "isotope")); atom.charge = static_cast<int>(num(o, "formalCharge"));
            atom.aromatic = o != nullptr && static_cast<bool>(o->getProperty("aromatic")); atom.stereo = o == nullptr ? std::string{} : o->getProperty("stereo").toString().toStdString();
            atoms.push_back(std::move(atom));
        }
    const int n = static_cast<int>(atoms.size());
    std::vector<Bond> bonds;
    if (const auto* list = detail == nullptr ? nullptr : detail->getProperty("bonds").getArray())
        for (const auto& v : *list)
        {
            const auto* o = v.getDynamicObject();
            Bond bond{static_cast<int>(num(o, "begin")), static_cast<int>(num(o, "end")), num(o, "order"), o == nullptr ? std::string{} : o->getProperty("stereo").toString().toStdString()};
            if (bond.a < 0 || bond.b < 0 || bond.a >= n || bond.b >= n || bond.a == bond.b) continue;
            atoms[static_cast<std::size_t>(bond.a)].bonds.emplace_back(bond.b, bond.order); atoms[static_cast<std::size_t>(bond.b)].bonds.emplace_back(bond.a, bond.order);
            bonds.push_back(std::move(bond));
        }
    const auto at = [&](int i) -> const Atom& { return atoms[static_cast<std::size_t>(i)]; };
    const auto degree = [&](int i) { return static_cast<int>(at(i).bonds.size()); };

    // --- size and composition -------------------------------------------------------------------
    p.heavyAtoms = heavy; p.molecularWeight = num(d, "molecularWeight"); p.massPerAtom = p.molecularWeight / heavy;
    const auto& e = a.elementCounts; // C,N,O,S,P,F,Cl,Br,I,other
    p.heteroFraction = (heavy - static_cast<double>(e[0])) / heavy; p.nitrogenFraction = static_cast<double>(e[1]) / heavy; p.oxygenFraction = static_cast<double>(e[2]) / heavy;
    p.sulfurAtoms = static_cast<double>(e[3]); p.phosphorusAtoms = static_cast<double>(e[4]);
    p.halogenAtoms = static_cast<double>(e[5] + e[6] + e[7] + e[8]);
    // Period of the halogen: F 1, Cl 2, Br 3, I 4. The load weights each atom by it.
    p.halogenLoad = static_cast<double>(e[5]) + 2.0 * static_cast<double>(e[6]) + 3.0 * static_cast<double>(e[7]) + 4.0 * static_cast<double>(e[8]);
    p.heaviestHalogen = e[8] ? 4 : e[7] ? 3 : e[6] ? 2 : e[5] ? 1 : 0;
    std::set<int> kinds;
    for (const auto& atom : atoms) { if (atom.number > 1) kinds.insert(atom.number); if (isMetal(atom.number)) ++p.metalAtoms; if (isMetalloid(atom.number)) ++p.metalloidAtoms; if (atom.isotope != 0) ++p.isotopeAtoms; }
    p.elementKinds = static_cast<double>(std::max<std::size_t>(1, kinds.size()));

    // --- bonding ----------------------------------------------------------------------------------
    p.aromaticFraction = num(d, "aromaticAtomFraction"); p.sp3Fraction = num(d, "fractionCsp3");
    p.carbonyls = motifs == nullptr ? 0.0 : num(motifs, "carbonyl");
    std::vector<char> unsaturated(static_cast<std::size_t>(n), 0);
    for (const auto& bond : bonds)
    {
        if (bond.order >= 1.4) unsaturated[static_cast<std::size_t>(bond.a)] = unsaturated[static_cast<std::size_t>(bond.b)] = 1;
        if (bond.order > 2.5) ++p.tripleBonds;
        else if (bond.order > 1.9 && at(bond.a).number != 8 && at(bond.b).number != 8 && !(at(bond.a).aromatic && at(bond.b).aromatic)) ++p.alkeneBonds;
    }
    for (const auto& atom : atoms) if (atom.aromatic && atom.number != 6) ++p.heteroaromaticAtoms;
    {   // Largest conjugated system: unsaturated atoms joined by any bond between two of them.
        std::vector<int> seen(static_cast<std::size_t>(n), 0); int largest = 0;
        for (int s = 0; s < n; ++s)
        {
            if (!unsaturated[static_cast<std::size_t>(s)] || seen[static_cast<std::size_t>(s)]) continue;
            int size = 0; std::queue<int> q; q.push(s); seen[static_cast<std::size_t>(s)] = 1;
            while (!q.empty()) { const int u = q.front(); q.pop(); ++size; for (const auto& [v, order] : at(u).bonds) if (unsaturated[static_cast<std::size_t>(v)] && !seen[static_cast<std::size_t>(v)]) { seen[static_cast<std::size_t>(v)] = 1; q.push(v); } }
            largest = std::max(largest, size);
        }
        p.conjugatedFraction = largest >= 2 ? static_cast<double>(largest) / std::max(1, n) : 0.0;
    }

    // --- fragments and all-pairs distances --------------------------------------------------------
    std::vector<std::vector<int>> dist(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) dist[static_cast<std::size_t>(i)] = distances(atoms, i);
    std::vector<int> component(static_cast<std::size_t>(n), -1), componentSize; int largestComponent = 0;
    for (int i = 0; i < n; ++i)
    {
        if (component[static_cast<std::size_t>(i)] >= 0) continue;
        int size = 0; for (int j = 0; j < n; ++j) if (dist[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] >= 0) { component[static_cast<std::size_t>(j)] = static_cast<int>(componentSize.size()); ++size; }
        componentSize.push_back(size); if (size > componentSize[static_cast<std::size_t>(largestComponent)]) largestComponent = static_cast<int>(componentSize.size()) - 1;
    }
    p.fragments = static_cast<double>(std::max<std::size_t>(1, componentSize.size()));
    p.largestFragmentFraction = n > 0 ? static_cast<double>(componentSize[static_cast<std::size_t>(largestComponent)]) / n : 1.0;
    std::vector<int> eccentricity(static_cast<std::size_t>(n), 0); int diameter = 0, radius = n;
    for (int i = 0; i < n; ++i)
    {
        eccentricity[static_cast<std::size_t>(i)] = *std::max_element(dist[static_cast<std::size_t>(i)].begin(), dist[static_cast<std::size_t>(i)].end());
        if (component[static_cast<std::size_t>(i)] == largestComponent) { diameter = std::max(diameter, eccentricity[static_cast<std::size_t>(i)]); radius = std::min(radius, eccentricity[static_cast<std::size_t>(i)]); }
    }
    if (n == 0) radius = 0;
    p.diameter = diameter;
    const int mainSize = n > 0 ? componentSize[static_cast<std::size_t>(largestComponent)] : 0;
    // 1 for an unbranched chain, falling toward 0 as the same atoms pack into rings and branches.
    p.elongation = mainSize > 1 ? static_cast<double>(diameter) / (mainSize - 1) : 0.0;
    // 1 at the graph centre, 0 on the periphery; atoms of minor fragments count as peripheral.
    const auto centrality = [&](int i) { return component[static_cast<std::size_t>(i)] != largestComponent ? 0.0 : diameter == radius ? 1.0 : 1.0 - static_cast<double>(eccentricity[static_cast<std::size_t>(i)] - radius) / (diameter - radius); };

    // --- rings ------------------------------------------------------------------------------------
    p.rings = num(d, "ringCount"); p.fusedPerRing = p.rings > 0 ? num(d, "fusedRingAdjacencyCount") / p.rings : 0.0;
    std::vector<int> smallestRing(static_cast<std::size_t>(n), 0);
    for (const auto& bond : bonds)
    {
        const int around = distances(atoms, bond.a, bond.a, bond.b)[static_cast<std::size_t>(bond.b)];
        if (around < 0) continue;
        for (const int end : {bond.a, bond.b}) { auto& s = smallestRing[static_cast<std::size_t>(end)]; s = s == 0 ? around + 1 : std::min(s, around + 1); }
    }
    int ringAtoms = 0;
    for (int i = 0; i < n; ++i) { const int s = smallestRing[static_cast<std::size_t>(i)]; if (s > 0) { ++ringAtoms; p.largestRing = std::max(p.largestRing, static_cast<double>(s)); if (s <= 4) ++p.smallRingAtoms; } }
    p.ringAtomFraction = n > 0 ? static_cast<double>(ringAtoms) / n : 0.0;

    // --- shape ------------------------------------------------------------------------------------
    int branched = 0, terminal = 0;
    for (int i = 0; i < n; ++i) { if (degree(i) >= 3) ++branched; if (degree(i) <= 1) ++terminal; }
    p.branchedFraction = n > 0 ? static_cast<double>(branched) / n : 0.0; p.terminalFraction = n > 0 ? static_cast<double>(terminal) / n : 0.0;
    p.flexibility = num(d, "rotatableBonds") / std::max(1.0, heavy - 1.0);
    {   // Longest run of acyclic carbons. Outside rings the carbon skeleton is a forest, so the
        // longest path of each tree is two breadth-first passes.
        std::vector<char> chain(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i) chain[static_cast<std::size_t>(i)] = at(i).number == 6 && smallestRing[static_cast<std::size_t>(i)] == 0;
        const auto far = [&](int from) { std::vector<int> dd(static_cast<std::size_t>(n), -1); std::queue<int> q; q.push(from); dd[static_cast<std::size_t>(from)] = 0; int last = from;
            while (!q.empty()) { const int u = q.front(); q.pop(); last = u; for (const auto& [v, order] : at(u).bonds) if (chain[static_cast<std::size_t>(v)] && dd[static_cast<std::size_t>(v)] < 0) { dd[static_cast<std::size_t>(v)] = dd[static_cast<std::size_t>(u)] + 1; q.push(v); } }
            return std::pair{last, dd[static_cast<std::size_t>(last)]}; };
        for (int i = 0; i < n; ++i) if (chain[static_cast<std::size_t>(i)]) p.longestChain = std::max(p.longestChain, static_cast<double>(far(far(i).first).second + 1));
    }
    {   // Symmetry classes by iterated neighbourhood refinement (Morgan). The integers only decide
        // which atoms are equivalent; the published value is the class count.
        std::vector<std::uint64_t> rank(static_cast<std::size_t>(n));
        const auto mix = [](std::uint64_t h, std::uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); return h; };
        for (int i = 0; i < n; ++i) rank[static_cast<std::size_t>(i)] = mix(mix(mix(mix(static_cast<std::uint64_t>(at(i).number), static_cast<std::uint64_t>(at(i).isotope)), static_cast<std::uint64_t>(at(i).charge + 8)), at(i).aromatic ? 1u : 0u), static_cast<std::uint64_t>(degree(i)));
        std::size_t classes = std::set<std::uint64_t>(rank.begin(), rank.end()).size();
        for (int round = 0; round < 32; ++round)
        {
            std::vector<std::uint64_t> next(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                std::vector<std::uint64_t> around;
                for (const auto& [v, order] : at(i).bonds) around.push_back(mix(rank[static_cast<std::size_t>(v)], static_cast<std::uint64_t>(std::llround(order * 2))));
                std::sort(around.begin(), around.end());
                auto h = rank[static_cast<std::size_t>(i)]; for (const auto v : around) h = mix(h, v); next[static_cast<std::size_t>(i)] = h;
            }
            const auto refined = std::set<std::uint64_t>(next.begin(), next.end()).size();
            rank = std::move(next); if (refined == classes) break; classes = refined;
        }
        p.symmetry = n > 1 ? 1.0 - static_cast<double>(classes - 1) / (n - 1) : 1.0;
    }

    // --- charge, isotopes, stereo ----------------------------------------------------------------
    int rectus = 0, sinister = 0;
    for (const auto& atom : atoms)
    {
        if (atom.charge > 0) ++p.positiveAtoms; if (atom.charge < 0) ++p.negativeAtoms;
        if (atom.stereo == "R") ++rectus; if (atom.stereo == "S") ++sinister;
    }
    p.chargedAtoms = p.positiveAtoms + p.negativeAtoms; p.netCharge = static_cast<double>(a.formalCharge);
    p.stereocenters = rectus + sinister; p.chiralBalance = rectus + sinister > 0 ? static_cast<double>(rectus - sinister) / (rectus + sinister) : 0.0;
    int entgegen = 0, zusammen = 0;
    for (const auto& bond : bonds) { if (bond.stereo == "STEREOE" || bond.stereo == "STEREOTRANS") ++entgegen; if (bond.stereo == "STEREOZ" || bond.stereo == "STEREOCIS") ++zusammen; }
    p.geometricBonds = entgegen + zusammen; p.geometricBalance = entgegen + zusammen > 0 ? static_cast<double>(entgegen - zusammen) / (entgegen + zusammen) : 0.0;

    // --- polarity ---------------------------------------------------------------------------------
    p.polarSurfacePerAtom = num(d, "tpsa") / heavy; p.logP = num(d, "logP"); p.donorsPerAtom = num(d, "hbd") / heavy; p.acceptorsPerAtom = num(d, "hba") / heavy;

    // --- functional groups ------------------------------------------------------------------------
    if (motifs != nullptr) { p.amides = num(motifs, "amide"); p.amines = num(motifs, "amine"); p.alcohols = num(motifs, "alcohol"); p.phenols = num(motifs, "phenol"); p.ethers = num(motifs, "ether"); p.arylHalides = num(motifs, "arylHalide"); }
    for (int i = 0; i < n; ++i)
    {
        const int z = at(i).number; int oxo = 0, terminalOxygen = 0, bridgingOxygen = 0, oxygens = 0;
        for (const auto& [v, order] : at(i).bonds)
        {
            if (at(v).number != 8) continue;
            ++oxygens; if (order > 1.9) ++oxo; else if (degree(v) == 1) ++terminalOxygen; else ++bridgingOxygen;
        }
        if (z == 6 && oxo == 1 && terminalOxygen >= 1) ++p.carboxyls;
        else if (z == 6 && oxo == 1 && bridgingOxygen >= 1) ++p.esters;
        if (z == 7 && oxygens >= 2 && oxo + terminalOxygen >= 2) ++p.nitros;
        if (z == 16 && oxo >= 2) ++p.sulfonyls;
        if (z == 16 && oxygens == 0) ++p.thios;
        if (z == 15 && oxygens >= 3) ++p.phosphates;
        if (z == 6) for (const auto& [v, order] : at(i).bonds) if (order > 2.5 && at(v).number == 7) ++p.nitriles;
    }

    // --- placement --------------------------------------------------------------------------------
    const auto meanOf = [&](auto&& include, auto&& value) { double sum = 0; int count = 0; for (int i = 0; i < n; ++i) if (include(i)) { sum += value(i); ++count; } return count ? sum / count : 0.0; };
    const auto isHetero = [&](int i) { return at(i).number > 1 && at(i).number != 6; };
    const auto isBranch = [&](int i) { return at(i).number == 6 && degree(i) >= 3; };
    const auto isRing = [&](int i) { return smallestRing[static_cast<std::size_t>(i)] > 0; };
    p.heteroCentrality = meanOf(isHetero, centrality); p.branchCentrality = meanOf(isBranch, centrality); p.ringCentrality = meanOf(isRing, centrality);
    // Mean distance from each atom of one kind to the nearest atom of another, over the diameter.
    const auto separation = [&](auto&& from, auto&& to)
    {
        double sum = 0; int count = 0;
        for (int i = 0; i < n; ++i)
        {
            if (!from(i)) continue;
            int nearest = -1;
            for (int j = 0; j < n; ++j) if (j != i && to(j) && dist[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] >= 0 && (nearest < 0 || dist[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] < nearest)) nearest = dist[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
            if (nearest >= 0) { sum += nearest; ++count; }
        }
        return count && diameter > 0 ? std::min(1.0, sum / count / diameter) : 0.0;
    };
    const auto isNitrogen = [&](int i) { return at(i).number == 7; };
    const auto isOxygen = [&](int i) { return at(i).number == 8; };
    p.nitrogenOxygenDistance = separation(isNitrogen, isOxygen); p.branchNitrogenDistance = separation(isBranch, isNitrogen); p.ringNitrogenDistance = separation(isRing, isNitrogen);
    p.chargeSeparation = separation([&](int i) { return at(i).charge > 0; }, [&](int i) { return at(i).charge < 0; });
    (void) isHalogen;
    return p;
}

juce::var encodeProfile(const MoleculeProfile& p)
{
    auto* o = new juce::DynamicObject; juce::var v(o);
#define X(name) o->setProperty(#name, p.name);
    IUPAC_PROFILE_FIELDS(X)
#undef X
    return v;
}
}
