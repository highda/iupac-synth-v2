#include "iupac/engine/Wavetables.hpp"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <complex>
#include <mutex>
#include <numbers>

namespace iupac::engine
{
namespace
{
constexpr std::size_t harmonics = wavetableMaximumSize / 2; // 1024: the most a 2048-sample cycle holds
constexpr std::size_t wavetableSize = wavetableMaximumSize;
constexpr double pi = std::numbers::pi_v<double>;
// One frame as a Fourier series: sum of sine[h] * sin(h x) + cosine[h] * cos(h x), h = 1..1024.
struct Spectrum { std::array<double, harmonics + 1> sine{}, cosine{}; };
using Complex = juce::dsp::Complex<float>;

double lerp(double a, double b, double t) { return a + (b - a) * t; }

// The four classic shapes as exact Fourier series, so no frame is ever built from a sampled
// discontinuity.
double sineSeries(std::size_t h) { return h == 1 ? 1.0 : 0.0; }
double triangleSeries(std::size_t h) { return h % 2 == 0 ? 0.0 : (h % 4 == 1 ? 1.0 : -1.0) * 8.0 / (pi * pi * static_cast<double>(h * h)); }
double sawSeries(std::size_t h) { return 2.0 / (pi * static_cast<double>(h)); }
double squareSeries(std::size_t h) { return h % 2 == 0 ? 0.0 : 4.0 / (pi * static_cast<double>(h)); }

// A cycle given in the time domain (only ever a smooth one) analysed back into its series.
Spectrum analyse(const juce::dsp::FFT& fft, const std::array<double, wavetableSize>& cycle)
{
    std::vector<Complex> in(wavetableSize), out(wavetableSize);
    for (std::size_t i = 0; i < wavetableSize; ++i) in[i] = Complex(static_cast<float>(cycle[i]), 0.0f);
    fft.perform(in.data(), out.data(), false);
    Spectrum s;
    // x[n] = sum c_h e^{+i h x}: cosine = 2 Re(c_h), sine = -2 Im(c_h). The top bin is dropped.
    for (std::size_t h = 1; h < harmonics; ++h) { s.cosine[h] = 2.0 * out[h].real() / wavetableSize; s.sine[h] = -2.0 * out[h].imag() / wavetableSize; }
    return s;
}

Spectrum generate(const juce::dsp::FFT& fft, std::size_t table, double t)
{
    Spectrum s;
    std::array<double, wavetableSize> cycle{};
    const auto phase = [](std::size_t i) { return 2.0 * pi * static_cast<double>(i) / wavetableSize; };
    switch (table)
    {
        case 0: // analog: sine -> triangle -> saw -> square
        {
            const double x = t * 3.0; const auto segment = std::min(2, static_cast<int>(x)); const double f = x - segment;
            for (std::size_t h = 1; h <= harmonics; ++h)
            {
                const std::array<double, 4> shapes{sineSeries(h), triangleSeries(h), sawSeries(h), squareSeries(h)};
                s.sine[h] = lerp(shapes[static_cast<std::size_t>(segment)], shapes[static_cast<std::size_t>(segment) + 1], f);
            }
            break;
        }
        case 1: // harmonics: a saw opening from its fundamental to the full series, a filter sweep with no filter
        {
            const double cutoff = std::pow(2.0, 9.0 * t); // harmonic 1 .. 512
            for (std::size_t h = 1; h <= harmonics; ++h) s.sine[h] = sawSeries(h) / (1.0 + std::pow(static_cast<double>(h) / cutoff, 6.0));
            break;
        }
        case 2: // pwm: pulse from 50 % duty down to 4 %
        {
            const double duty = lerp(0.5, 0.04, t);
            for (std::size_t h = 1; h <= harmonics; ++h) s.cosine[h] = 4.0 / (pi * static_cast<double>(h)) * std::sin(pi * static_cast<double>(h) * duty);
            break;
        }
        case 3: // formant: a glottal-like series under three moving vowel formants, A E I O U
        {
            // Formant centres in harmonic numbers of a 110 Hz voice (F / 110).
            static constexpr std::array<std::array<double, 3>, 5> vowels{{{7.3, 10.5, 26.4}, {3.6, 18.2, 25.5}, {2.5, 19.5, 26.8}, {4.1, 7.3, 25.7}, {3.0, 6.4, 24.5}}};
            const double x = t * 4.0; const auto a = std::min<std::size_t>(3, static_cast<std::size_t>(x)); const double f = x - static_cast<double>(a);
            for (std::size_t h = 1; h <= harmonics; ++h)
            {
                double envelope = 0.06;
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const double centre = lerp(vowels[a][k], vowels[a + 1][k], f), width = 1.1 + 0.12 * centre;
                    const double d = (static_cast<double>(h) - centre) / width;
                    envelope += (k == 0 ? 1.0 : k == 1 ? 0.7 : 0.35) * std::exp(-0.5 * d * d);
                }
                s.sine[h] = envelope / std::pow(static_cast<double>(h), 0.9);
            }
            s.sine[1] = std::max(s.sine[1], 0.5);
            break;
        }
        case 4: // organ: drawbar registrations, flute -> octaves -> full -> reedy
        {
            static constexpr std::array<std::size_t, 9> partials{1, 2, 3, 4, 5, 6, 8, 10, 12};
            static constexpr std::array<std::array<double, 9>, 4> registrations{{{1, 0, 0, 0, 0, 0, 0, 0, 0}, {1, .8, 0, .6, 0, 0, .4, 0, 0}, {1, .9, .7, .8, .4, .5, .6, .2, .3}, {.7, .3, 1, .2, .8, .3, .2, .6, .5}}};
            const double x = t * 3.0; const auto a = std::min<std::size_t>(2, static_cast<std::size_t>(x)); const double f = x - static_cast<double>(a);
            for (std::size_t k = 0; k < partials.size(); ++k) s.sine[partials[k]] = lerp(registrations[a][k], registrations[a + 1][k], f);
            break;
        }
        case 5: // sync: a Hann-windowed slave sine swept from the fundamental to eight times it
        {
            const double ratio = lerp(1.0, 8.0, t);
            for (std::size_t i = 0; i < wavetableSize; ++i) cycle[i] = std::sin(ratio * phase(i)) * (0.5 - 0.5 * std::cos(phase(i)));
            s = analyse(fft, cycle);
            s.cosine[0] = 0;
            break;
        }
        case 6: // fm: two operators at 1:1, index 0 -> 7 — always harmonic, sine to brass
        {
            const double index = 7.0 * t;
            for (std::size_t i = 0; i < wavetableSize; ++i) cycle[i] = std::sin(phase(i) + index * std::sin(phase(i)));
            s = analyse(fft, cycle);
            break;
        }
        default: // digital: a fundamental under a comb that starts on the even harmonics and sweeps
        {
            const double comb = lerp(0.5, 0.08, t);
            for (std::size_t h = 2; h <= harmonics; ++h) { const double c = std::cos(pi * static_cast<double>(h) * comb); s.sine[h] = c * c / std::pow(static_cast<double>(h), 0.8); }
            s.sine[1] = 1.0;
            break;
        }
    }
    return s;
}

// A sampled cycle analysed by direct Fourier sums, then rotated so its fundamental starts at
// phase zero. The rotation is a pure time shift — the cycle's shape is untouched — and it is what
// lets two adjacent cycles of one instrument crossfade without their fundamentals cancelling.
Spectrum analyseSampled(const std::array<std::int16_t, sampledCycleSamples>& cycle)
{
    Spectrum s;
    const auto count = static_cast<double>(sampledCycleSamples);
    for (std::size_t h = 1; h < sampledCycleSamples / 2; ++h)
    {
        double sine = 0, cosine = 0;
        for (std::size_t n = 0; n < sampledCycleSamples; ++n)
        {
            const double angle = 2.0 * pi * static_cast<double>(h * n % sampledCycleSamples) / count, x = cycle[n] / 32768.0;
            sine += x * std::sin(angle); cosine += x * std::cos(angle);
        }
        s.sine[h] = 2.0 * sine / count; s.cosine[h] = 2.0 * cosine / count;
    }
    // sine sin(x) + cosine cos(x) = A sin(x + shift); advance every harmonic h by -h * shift.
    const double shift = std::atan2(s.cosine[1], s.sine[1]);
    for (std::size_t h = 1; h < sampledCycleSamples / 2; ++h)
    {
        const double c = std::cos(static_cast<double>(h) * shift), d = std::sin(static_cast<double>(h) * shift);
        const double sine = s.sine[h] * c + s.cosine[h] * d, cosine = s.cosine[h] * c - s.sine[h] * d;
        s.sine[h] = sine; s.cosine[h] = cosine;
    }
    return s;
}

Wavetable build(std::size_t table)
{
    Wavetable result;
    result.samples.assign(wavetableFrames * wavetableFrameStride, 0.0f);
    const juce::dsp::FFT analysis(11);
    std::vector<Spectrum> sampled;
    if (table >= synthesizedWavetableCount)
    {
        const auto& source = sampledWavetables[table - synthesizedWavetableCount];
        for (std::size_t i = 0; i < std::clamp<std::size_t>(source.frames, 1, wavetableFrames); ++i) sampled.push_back(analyseSampled(source.cycles[i]));
    }
    std::vector<Complex> in(wavetableSize), out(wavetableSize);
    for (std::size_t frame = 0; frame < wavetableFrames; ++frame)
    {
        const double t = static_cast<double>(frame) / (wavetableFrames - 1);
        Spectrum spectrum;
        if (sampled.empty()) spectrum = generate(analysis, table, t);
        else
        {
            // A folder with fewer than sixteen cycles is spread across the sixteen frames.
            const double x = t * static_cast<double>(sampled.size() - 1);
            const auto a = std::min(sampled.size() - 1, static_cast<std::size_t>(x)), b = std::min(sampled.size() - 1, a + 1);
            const double f = x - static_cast<double>(a);
            for (std::size_t h = 1; h <= harmonics; ++h) { spectrum.sine[h] = lerp(sampled[a].sine[h], sampled[b].sine[h], f); spectrum.cosine[h] = lerp(sampled[a].cosine[h], sampled[b].cosine[h], f); }
        }
        float peak = 0.0f;
        for (std::size_t level = 0; level < wavetableLevels; ++level)
        {
            const auto size = wavetableLevelSize(level);
            const juce::dsp::FFT fft(static_cast<int>(std::countr_zero(size)));
            // The top bin of a cycle cannot carry a phase, so a level never uses it.
            const auto limit = std::min(size / 2 - 1, wavetableLevelHarmonics(level));
            std::fill(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(size), Complex{});
            for (std::size_t h = 1; h <= limit; ++h)
            {
                // sine sin(hx) + cosine cos(hx) = c e^{ihx} + conj(c) e^{-ihx}, c = (cosine - i sine) / 2.
                const Complex c(static_cast<float>(spectrum.cosine[h] * 0.5), static_cast<float>(-spectrum.sine[h] * 0.5));
                in[h] = c; in[size - h] = std::conj(c);
            }
            fft.perform(in.data(), out.data(), true);
            // The pinned FFT's inverse scaling is not part of its contract and differs by size, so
            // each level is rescaled by its own length and the frame by its full-band peak.
            const auto scale = static_cast<float>(size);
            if (level == 0) { for (std::size_t i = 0; i < size; ++i) peak = std::max(peak, std::abs(out[i].real() * scale)); if (!(peak > 0.0f)) peak = 1.0f; }
            auto* destination = result.samples.data() + frame * wavetableFrameStride + wavetableLevelOffset(level);
            for (std::size_t i = 0; i < size; ++i) destination[i] = out[i].real() * scale / peak;
            destination[size] = destination[0];
        }
    }
    return result;
}

std::array<std::atomic<const Wavetable*>, wavetableCount> built{};
std::mutex buildMutex;
}

const Wavetable& wavetable(std::size_t table)
{
    table = std::min(table, wavetableCount - 1);
    if (const auto* ready = built[table].load(std::memory_order_acquire)) return *ready;
    const std::scoped_lock lock(buildMutex);
    if (const auto* ready = built[table].load(std::memory_order_acquire)) return *ready;
    // Built tables live for the process: a voice may still be reading one after its patch is gone.
    const auto* made = new Wavetable(build(table));
    built[table].store(made, std::memory_order_release);
    return *made;
}

const Wavetable* wavetableIfBuilt(std::size_t table) noexcept { return table < wavetableCount ? built[table].load(std::memory_order_acquire) : nullptr; }
}
