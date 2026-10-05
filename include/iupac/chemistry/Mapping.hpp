#pragma once

#include "iupac/chemistry/Analysis.hpp"
#include "iupac/chemistry/Profile.hpp"
#include "iupac/domain/Patch.hpp"

#include <array>
#include <optional>
#include <string>

namespace iupac::chemistry
{
// Mapper version 3 / SonicIntent version 2 (D13, #168): the ground-up rework. The seven axes of
// version 1 are gone; the intent is now the structural profile plus the perceptual traits below.
inline constexpr int sonicIntentVersion = 2;
inline constexpr int mappingVersion = 3;

// Perceptual traits, each in [0, 1] (`handed` in [-1, 1]). A trait is one profile quantity, or a
// documented blend of two or three, rescaled so that it spreads across the molecules people
// actually type rather than piling up at one end. They carry no synth vocabulary.
#define IUPAC_TRAIT_FIELDS(X) \
    X(size) X(weight) X(aromatic) X(saturated) X(unsaturated) X(conjugated) X(cyclic) X(alicyclic) X(fused) X(strained) \
    X(polar) X(lipophilic) X(flexible) X(linear) X(branched) X(chain) X(symmetric) X(diverse) \
    X(charged) X(zwitterionic) X(halogenated) X(heavyHalogen) X(metallic) X(isotopic) X(chiral) X(handed) X(geometric) X(cisTrans) \
    X(donor) X(acceptor) X(heteroaromatic) X(salt) X(inorganic) \
    X(amide) X(amine) X(hydroxyl) X(phenolic) X(ether) X(carboxyl) X(ester) X(carbonyl) X(nitro) X(nitrile) X(sulfonyl) X(thio) X(sulfur) X(phosphorus) \
    X(bright) X(placement)

struct Traits
{
#define X(name) double name{};
    IUPAC_TRAIT_FIELDS(X)
#undef X
};

struct SonicIntent
{
    MoleculeProfile profile;
    Traits traits;
};

struct GenerationResult
{
    std::optional<SonicIntent> intent;
    std::optional<domain::Patch> patch;
    juce::var trace;
    std::string error;
    explicit operator bool() const noexcept { return patch.has_value(); }
};

[[nodiscard]] SonicIntent project(const Analysis&);
[[nodiscard]] juce::var encodeSonicIntent(const SonicIntent&);
[[nodiscard]] GenerationResult generate(const Analysis&);
}
