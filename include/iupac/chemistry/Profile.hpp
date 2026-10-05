#pragma once

#include "iupac/chemistry/Analysis.hpp"

namespace iupac::chemistry
{
// Mapper v3 (D13, #168). Everything the mapper may read about a molecule, derived once from the
// Analysis v1 graph (atoms, bonds, motif counts, RDKit descriptors). Nothing here parses SMILES,
// consults a database or hashes identity into a value: each field is a count, a fraction or a
// topological distance a chemist could recompute by hand from the drawn structure.
#define IUPAC_PROFILE_FIELDS(X) \
    /* size and composition */ \
    X(heavyAtoms) X(molecularWeight) X(massPerAtom) X(heteroFraction) X(nitrogenFraction) X(oxygenFraction) \
    X(sulfurAtoms) X(phosphorusAtoms) X(halogenAtoms) X(halogenLoad) X(heaviestHalogen) X(metalAtoms) X(metalloidAtoms) X(elementKinds) \
    /* bonding */ \
    X(aromaticFraction) X(sp3Fraction) X(alkeneBonds) X(tripleBonds) X(carbonyls) X(conjugatedFraction) X(heteroaromaticAtoms) \
    /* rings */ \
    X(rings) X(ringAtomFraction) X(fusedPerRing) X(smallRingAtoms) X(largestRing) \
    /* shape */ \
    X(fragments) X(largestFragmentFraction) X(diameter) X(elongation) X(branchedFraction) X(terminalFraction) X(longestChain) X(symmetry) X(flexibility) \
    /* charge, isotopes, stereo */ \
    X(chargedAtoms) X(positiveAtoms) X(negativeAtoms) X(netCharge) X(isotopeAtoms) X(stereocenters) X(chiralBalance) X(geometricBonds) X(geometricBalance) \
    /* polarity */ \
    X(polarSurfacePerAtom) X(logP) X(donorsPerAtom) X(acceptorsPerAtom) \
    /* functional groups (counts) */ \
    X(amides) X(amines) X(alcohols) X(phenols) X(ethers) X(carboxyls) X(esters) X(nitros) X(nitriles) X(sulfonyls) X(thios) X(phosphates) X(arylHalides) \
    /* placement: where things sit in the graph, 0..1 */ \
    X(heteroCentrality) X(branchCentrality) X(ringCentrality) X(nitrogenOxygenDistance) X(branchNitrogenDistance) X(ringNitrogenDistance) X(chargeSeparation)

struct MoleculeProfile
{
#define X(name) double name{};
    IUPAC_PROFILE_FIELDS(X)
#undef X
};

[[nodiscard]] MoleculeProfile profile(const Analysis&);
[[nodiscard]] juce::var encodeProfile(const MoleculeProfile&);
}
