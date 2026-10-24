#include <silt/Color.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr double maxWowMs = 3.0, maxFlutterMs = 0.25;

float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

// Glides toward the target and lands on it exactly, so a stage returned to
// its neutral value is skipped again.
void glide (float& value, float target, float coefficient) noexcept
{
    value += coefficient * (target - value);
    if (std::abs (target - value) < 1.0e-5f)
        value = target;
}
} // namespace

void Color::prepare (double rate)
{
    sampleRate = rate;
    int size = 1;
    while (size < (int) (0.02 * rate))   // twice the deepest wow plus flutter, with room
        size *= 2;
    for (auto& line : wowLine)
        line.assign ((size_t) size, 0.0f);
    reset();
}

void Color::reset() noexcept
{
    for (auto& line : wowLine)
        std::fill (line.begin(), line.end(), 0.0f);
    wowWrite = 0;
    wowPhase = flutterPhase = 0.0;
    flutterDrift = flutterTarget = 0.0f;
    toneLow = holdValue = noiseState = {};
    holdPhase = 1.0;
    detector = -120.0f;
    compGain = 0.0f;
    level = 0.0f;
    primed = false;
}

void Color::process (float* left, float* right, int numSamples, const ColorParams& p) noexcept
{
    if (! primed)
    {
        primed = true;
        now = p;
    }

    const float fs = (float) sampleRate;
    const auto coefficient = [fs] (float seconds) { return 1.0f - std::exp (-1.0f / (seconds * fs)); };
    const float knob = coefficient (0.02f);
    const float toneCoefficient = 1.0f - std::exp (-(float) twoPi * 800.0f / fs);
    const float detectAttack = coefficient (0.005f), detectRelease = coefficient (0.12f);
    const float grAttack = coefficient (0.005f), grRelease = coefficient (0.15f);
    const float levelAttack = coefficient (0.001f), levelRelease = coefficient (0.2f);
    const float hissCoefficient = 1.0f - std::exp (-(float) twoPi * 7000.0f / fs);
    const int lineMask = (int) wowLine[0].size() - 1;
    const double wowStep = 0.55 / sampleRate, flutterStep = 6.3 / sampleRate;
    const float rateTarget = std::clamp (p.rateHz, 100.0f, 1.0e6f);

    for (int i = 0; i < numSamples; ++i)
    {
        glide (now.drive, std::clamp (p.drive, 0.0f, 1.0f), knob);
        glide (now.tone, std::clamp (p.tone, -1.0f, 1.0f), knob);
        glide (now.bits, std::clamp (p.bits, 2.0f, 16.0f), knob);
        glide (now.comp, std::clamp (p.comp, 0.0f, 1.0f), knob);
        glide (now.noise, std::clamp (p.noise, 0.0f, 1.0f), knob);
        glide (now.wow, std::clamp (p.wow, 0.0f, 1.0f), knob);
        glide (now.mix, std::clamp (p.mix, 0.0f, 1.0f), knob);
        now.rateHz *= std::pow (rateTarget / now.rateHz, knob);
        if (std::abs (now.rateHz / rateTarget - 1.0f) < 1.0e-5f)
            now.rateHz = rateTarget;

        const float dry[2] { left[i], right[i] };
        float x[2] { dry[0], dry[1] };
        bool touched = false;

        // Wow and flutter: a delay swinging between zero and its depth, so at
        // zero depth it is no delay at all.
        for (int ch = 0; ch < 2; ++ch)
            wowLine[(size_t) ch][(size_t) wowWrite] = x[ch];
        wowPhase += wowStep;
        flutterPhase += flutterStep;
        wowPhase -= std::floor (wowPhase);
        flutterPhase -= std::floor (flutterPhase);
        if (flutterPhase < flutterStep)   // a new flutter depth each cycle, so it never repeats exactly
            flutterTarget = 0.5f + random.next01();
        flutterDrift += 0.001f * (flutterTarget - flutterDrift);
        if (now.wow > 0.0f)
        {
            touched = true;
            const double depth = now.wow * sampleRate / 1000.0;
            const double delay = depth * (maxWowMs * (1.0 - std::cos (twoPi * wowPhase))
                                          + maxFlutterMs * flutterDrift * (1.0 - std::cos (twoPi * flutterPhase)));
            const int whole = (int) delay;
            const float fraction = (float) (delay - whole);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto& line = wowLine[(size_t) ch];
                const float a = line[(size_t) ((wowWrite - whole) & lineMask)];
                const float b = line[(size_t) ((wowWrite - whole - 1) & lineMask)];
                x[ch] = a + (b - a) * fraction;
            }
        }
        wowWrite = (wowWrite + 1) & lineMask;

        // Drive: up to +30 dB into tanh, with a small offset for even harmonics,
        // leveled so loud and quiet settings sit close.
        if (now.drive > 0.0f)
        {
            touched = true;
            const float gain = dbToGain (30.0f * now.drive);
            const float bias = 0.2f * now.drive;
            const float makeup = 1.0f / std::pow (gain, 0.8f);
            const float blend = std::min (1.0f, now.drive * 10.0f);
            for (auto& s : x)
                s += blend * ((std::tanh (gain * s + bias) - std::tanh (bias)) * makeup - s);
        }

        // Tone: a tilt around 800 Hz. The split runs always, so it starts clean.
        {
            const float high = dbToGain (6.0f * now.tone), low = 1.0f / high;
            for (int ch = 0; ch < 2; ++ch)
            {
                toneLow[(size_t) ch] += toneCoefficient * (x[ch] - toneLow[(size_t) ch]);
                if (now.tone != 0.0f)
                    x[ch] = toneLow[(size_t) ch] * low + (x[ch] - toneLow[(size_t) ch]) * high;
            }
            touched |= now.tone != 0.0f;
        }

        // Bits: rounding to 2^(bits-1) steps either side of zero.
        if (now.bits < 16.0f)
        {
            touched = true;
            const float steps = std::pow (2.0f, now.bits - 1.0f);
            for (auto& s : x)
                s = std::round (s * steps) / steps;
        }

        // Rate: sample and hold.
        if (now.rateHz < std::min (fs, rateOff))
        {
            touched = true;
            holdPhase += now.rateHz / fs;
            if (holdPhase >= 1.0)
            {
                holdPhase -= std::floor (holdPhase);
                holdValue = { x[0], x[1] };
            }
            x[0] = holdValue[0];
            x[1] = holdValue[1];
        }
        else
        {
            holdPhase = 1.0;   // the next engaged sample is taken at once
        }

        // Comp: stereo-linked, peak-sensing; threshold falls and ratio rises
        // together, with half the full-scale gain reduction made back up.
        {
            const float peak = std::max (std::abs (x[0]), std::abs (x[1]));
            const float peakDb = 20.0f * std::log10 (std::max (peak, 1.0e-6f));
            detector += (peakDb > detector ? detectAttack : detectRelease) * (peakDb - detector);
            if (now.comp > 0.0f)
            {
                touched = true;
                const float threshold = -30.0f * now.comp, ratio = 1.0f + 7.0f * now.comp;
                const float over = detector - threshold;
                const float reduction = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
                compGain += (reduction < compGain ? grAttack : grRelease) * (reduction - compGain);
                const float gain = dbToGain (compGain - 0.5f * threshold * (1.0f - 1.0f / ratio));
                x[0] *= gain;
                x[1] *= gain;
            }
            else
                compGain = 0.0f;
        }

        // Noise: hiss up to −20 dB under the signal, riding its level.
        {
            const float peak = std::max (std::abs (x[0]), std::abs (x[1]));
            level += (peak > level ? levelAttack : levelRelease) * (peak - level);
            if (now.noise > 0.0f)
            {
                touched = true;
                const float amount = 0.1f * now.noise * now.noise * level * 1.7f;   // 1.7: filtered white back to unit RMS
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& n = noiseState[(size_t) ch];
                    n += hissCoefficient * ((2.0f * random.next01() - 1.0f) * 1.732f - n);
                    x[ch] += amount * n;
                }
            }
        }

        if (touched)
        {
            left[i] = dry[0] + now.mix * (x[0] - dry[0]);
            right[i] = dry[1] + now.mix * (x[1] - dry[1]);
        }
    }
}

} // namespace silt
