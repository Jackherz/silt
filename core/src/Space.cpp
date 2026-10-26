#include <silt/Space.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double maxTankMs = 150.0;      // the shortest line at full SIZE; the longest is twice that
constexpr double maxPreDelayMs = 35.0;
constexpr double modDepthMs = 0.25;      // per-line modulation, either way
constexpr double shiftWindowMs = 80.0;   // shimmer grains

float onePole (double hz, double rate) { return (float) (1.0 - std::exp (-2.0 * pi * hz / rate)); }

// In place, orthonormal: a fast Hadamard transform scaled by 1/sqrt(8).
void hadamard (std::array<float, Space::channels>& x) noexcept
{
    for (int h = 1; h < Space::channels; h *= 2)
        for (int i = 0; i < Space::channels; i += 2 * h)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[(size_t) j], b = x[(size_t) (j + h)];
                x[(size_t) j] = a + b;
                x[(size_t) (j + h)] = a - b;
            }
    for (auto& v : x)
        v *= 0.35355339f;
}

// Glides toward the target and lands on it exactly.
void glide (float& value, float target, float coefficient) noexcept
{
    value += coefficient * (target - value);
    if (std::abs (target - value) < 1.0e-6f * std::max (1.0f, std::abs (target)))
        value = target;
}
} // namespace

// --- Line ---------------------------------------------------------------------------

void Space::Line::allocate (int maxDelaySamples)
{
    int size = 4;
    while (size < maxDelaySamples + 4)
        size *= 2;
    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;
    write = 0;
}

void Space::Line::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
}

float Space::Line::readLinear (double delay) const noexcept
{
    const int whole = (int) delay;
    const float f = (float) (delay - whole);
    const float a = at (whole);
    return a + (at (whole + 1) - a) * f;
}

float Space::Line::read (double delay) const noexcept
{
    const int whole = (int) delay;
    const float f = (float) (delay - whole);
    const float xm1 = at (whole - 1), x0 = at (whole), x1 = at (whole + 1), x2 = at (whole + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// --- Space ----------------------------------------------------------------------------

void Space::prepare (double rate)
{
    sampleRate = rate;
    const auto samples = [rate] (double ms) { return (int) std::ceil (ms * rate / 1000.0); };

    for (auto& line : delayLines)
        line.allocate ((int) std::ceil (maxDelaySeconds * rate) + 4);
    for (auto& line : preDelay)
        line.allocate (samples (maxPreDelayMs) + 4);
    for (size_t step = 0; step < diffusers.size(); ++step)
        for (auto& line : diffusers[step])
            line.allocate (samples (maxTankMs / std::pow (2.0, (double) step + 1.0)) + 4);
    for (auto& line : tank)
        line.allocate (samples (2.0 * maxTankMs + 2.0 * modDepthMs) + 8);
    shiftLine.allocate (samples (shiftWindowMs) + 8);

    // Fixed, so the room is the same every time: where each diffuser line sits
    // in its slice of the step's range, its polarity, and each tank line's
    // modulation rate (spread so no two lines move together).
    Random r (0x5ace);
    for (size_t step = 0; step < diffusers.size(); ++step)
        for (int c = 0; c < channels; ++c)
        {
            diffuserShare[step][(size_t) c] = ((float) c + r.next01()) / (float) channels;
            diffuserFlip[step][(size_t) c] = r.next01() < 0.5f;
        }
    for (int c = 0; c < channels; ++c)
        modRate[(size_t) c] = (0.35 + 0.9 * (double) c / channels + 0.05 * r.next01()) / rate;

    reset();
}

void Space::reset() noexcept
{
    for (auto& line : delayLines)
        line.clear();
    for (auto& line : preDelay)
        line.clear();
    for (auto& step : diffusers)
        for (auto& line : step)
            line.clear();
    for (auto& line : tank)
        line.clear();
    shiftLine.clear();
    loopLow = loopHigh = inputLow = inputHigh = {};
    tankState = {};
    for (int c = 0; c < channels; ++c)
        modPhase[(size_t) c] = (double) c / channels;
    modNow = modNext = {};
    shiftPhase = 0.0;
    shimmerHigh = shimmerLow = tankEnergy = shimmerInject = 0.0f;
    clearPosition = -1;
}

// Lines to clear one per block once idle, so going idle never stalls a block.
void Space::clearSome (int amount) noexcept
{
    const int total = 2 + 2 + (int) (diffusers.size() * channels) + channels + 1;
    for (int n = 0; n < amount && clearPosition >= 0; ++n)
    {
        int i = clearPosition;
        if (i < 2)
            delayLines[(size_t) i].clear();
        else if ((i -= 2) < 2)
            preDelay[(size_t) i].clear();
        else if ((i -= 2) < (int) diffusers.size() * channels)
            diffusers[(size_t) (i / channels)][(size_t) (i % channels)].clear();
        else if ((i -= (int) diffusers.size() * channels) < channels)
            tank[(size_t) i].clear();
        else
            shiftLine.clear();
        clearPosition = clearPosition + 1 < total ? clearPosition + 1 : -1;
    }
}

void Space::updateReverb() noexcept
{
    const double base = (25.0 + (maxTankMs - 25.0) * now.size) * sampleRate / 1000.0;
    const double rt = std::max (0.1, (double) now.decaySeconds);
    const double highRatio = 0.15 + 0.75 * now.tone;   // high-frequency RT60 against low
    double meanGain = 0.0;

    for (int c = 0; c < channels; ++c)
    {
        const double length = base * std::pow (2.0, (double) c / channels);
        tankLength[(size_t) c] = (float) length;

        // Loop gain for this length's share of RT60, then a one-pole shelf whose
        // top end decays at the high-frequency RT60 (Jot). FREEZE: gain 1, no damping.
        const double g = std::pow (10.0, -3.0 * length / (sampleRate * rt));
        const double top = std::pow (g, 1.0 / highRatio - 1.0);
        const double a = (1.0 - top) / (1.0 + top);
        tankGain[(size_t) c] = (float) (g + (1.0 - g) * now.freeze);
        tankSmooth[(size_t) c] = (float) (a * (1.0 - now.freeze));
        meanGain += g / channels;
    }

    diffuserRange = (float) base;
    preDelaySamples = (float) std::max (2.0, maxPreDelayMs * now.size * sampleRate / 1000.0);
    // Long tails hold more energy: trim them part of the way back.
    outputTrim = (float) (0.7 * std::pow (1.0 - meanGain * meanGain, 0.25));
    inputLowCoefficient = onePole (2000.0 * std::pow (9.0, now.tone), sampleRate);
    loopLowCoefficient = onePole (1500.0 * std::pow (10.0, now.tone), sampleRate);
    loopHighCoefficient = onePole (150.0 - 110.0 * now.tone, sampleRate);
}

void Space::process (float* left, float* right, int numSamples, const SpaceParams& p) noexcept
{
    const float fs = (float) sampleRate;
    const float maxDelay = (float) (maxDelaySeconds * sampleRate);
    const float delayTarget = std::clamp (p.delayMs * fs / 1000.0f, 2.0f, maxDelay);
    const float levelTarget = std::max (p.delayLevel, p.reverbLevel);

    if (! primed)
    {
        primed = true;
        now.delaySamples = delayTarget;
        now.feedback = p.feedback;
        now.pingPong = p.pingPong ? 1.0f : 0.0f;
        now.size = p.size;
        now.decaySeconds = p.decaySeconds;
        now.tone = p.tone;
        now.freeze = p.freeze ? 1.0f : 0.0f;
    }

    if (! active)
    {
        if (levelTarget <= 0.0f)
        {
            clearSome (1);
            return;   // exact bypass
        }
        clearSome (1 << 20);   // finish any clearing left from going idle
        active = true;
        loopLow = loopHigh = inputLow = inputHigh = {};
        tankState = {};
        shimmerHigh = shimmerLow = tankEnergy = shimmerInject = 0.0f;
    }

    const auto coefficient = [fs] (float seconds) { return 1.0f - std::exp (-1.0f / (seconds * fs)); };
    const float fast = coefficient (0.02f), timeGlide = coefficient (0.08f), slow = coefficient (0.3f), medium = coefficient (0.05f);
    const float duckAttack = coefficient (0.01f), duckRelease = coefficient (0.12f);   // back within about half a second
    const float shimmerHighCoefficient = onePole (300.0, sampleRate), shimmerLowCoefficient = onePole (8000.0, sampleRate);
    const float energyCoefficient = coefficient (0.05f);
    const float inputHighCoefficient = onePole (80.0, sampleRate);
    const double shiftWindow = shiftWindowMs * sampleRate / 1000.0;
    const double modDepth = modDepthMs * sampleRate / 1000.0;
    const float maxSizeStep = (float) (0.06 / (2.0 * (maxTankMs - 25.0) * sampleRate / 1000.0));
    constexpr int controlInterval = 16;

    for (int i = 0; i < numSamples; ++i)
    {
        glide (now.delaySamples, delayTarget, timeGlide);
        glide (now.feedback, std::clamp (p.feedback, 0.0f, 1.0f), fast);
        glide (now.delayLevel, std::clamp (p.delayLevel, 0.0f, 1.0f), fast);
        glide (now.reverbLevel, std::clamp (p.reverbLevel, 0.0f, 1.0f), fast);
        glide (now.pingPong, p.pingPong ? 1.0f : 0.0f, fast);
        {
            // SIZE re-pitches the tail as its lines change length (a doppler
            // glide); capped so the longest line never moves faster than about
            // a semitone (6%), however fast the knob turns.
            float sized = now.size;
            glide (sized, std::clamp (p.size, 0.0f, 1.0f), slow);
            now.size += std::clamp (sized - now.size, -maxSizeStep, maxSizeStep);
        }
        glide (now.decaySeconds, std::clamp (p.decaySeconds, 0.1f, 60.0f), medium);
        glide (now.tone, std::clamp (p.tone, 0.0f, 1.0f), medium);
        glide (now.shimmer, std::clamp (p.shimmer, 0.0f, 1.0f), fast);
        glide (now.freeze, p.freeze ? 1.0f : 0.0f, medium);
        if (i % controlInterval == 0)
        {
            updateReverb();
            // Line modulation: a sine per line, worked out per block and ramped between.
            for (int c = 0; c < channels; ++c)
            {
                modNow[(size_t) c] = modNext[(size_t) c];
                modPhase[(size_t) c] += modRate[(size_t) c] * controlInterval;
                modPhase[(size_t) c] -= std::floor (modPhase[(size_t) c]);
                modNext[(size_t) c] = (float) (modDepth * (1.0 + std::sin (2.0 * pi * modPhase[(size_t) c])));
            }
        }
        const float along = (float) (i % controlInterval + 1) / controlInterval;

        const float inLeft = left[i], inRight = right[i];
        const float open = 1.0f - now.freeze;   // FREEZE fades the input out

        // --- Delay
        const float delayed[2] { delayLines[0].read (now.delaySamples), delayLines[1].read (now.delaySamples) };
        float loop[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            loopHigh[(size_t) ch] += loopHighCoefficient * (delayed[ch] - loopHigh[(size_t) ch]);
            loopLow[(size_t) ch] += loopLowCoefficient * (delayed[ch] - loopHigh[(size_t) ch] - loopLow[(size_t) ch]);
            const float shaped = now.feedback * std::tanh (loopLow[(size_t) ch]);
            loop[ch] = shaped + (delayed[ch] - shaped) * now.freeze;   // frozen: the loop as it is, at unity
        }
        const float mono = 0.5f * (inLeft + inRight) * open;
        const float pp = now.pingPong;
        delayLines[0].push ((1.0f - pp) * (inLeft * open + loop[0]) + pp * (mono + loop[1]));
        delayLines[1].push ((1.0f - pp) * (inRight * open + loop[1]) + pp * loop[0]);

        // --- Reverb input: the dry signal and the repeats, band-limited by TONE.
        float send[2] { (inLeft + now.delayLevel * delayed[0]) * open, (inRight + now.delayLevel * delayed[1]) * open };
        for (int ch = 0; ch < 2; ++ch)
        {
            inputHigh[(size_t) ch] += inputHighCoefficient * (send[ch] - inputHigh[(size_t) ch]);
            inputLow[(size_t) ch] += inputLowCoefficient * (send[ch] - inputHigh[(size_t) ch] - inputLow[(size_t) ch]);
            preDelay[(size_t) ch].push (inputLow[(size_t) ch]);
            send[ch] = preDelay[(size_t) ch].read (preDelaySamples);
        }

        std::array<float, channels> x;
        for (int c = 0; c < channels; ++c)
            x[(size_t) c] = send[c % 2] * ((c / 2) % 2 == 0 ? 1.0f : -1.0f) + shimmerInject * (c % 2 == 0 ? 1.0f : -1.0f);

        // Diffusion: four steps, each half the length of the one before.
        for (size_t step = 0; step < diffusers.size(); ++step)
        {
            const float range = diffuserRange / (float) (2 << step);
            for (int c = 0; c < channels; ++c)
            {
                auto& line = diffusers[step][(size_t) c];
                line.push (x[(size_t) c]);
                x[(size_t) c] = line.readLinear (std::max (2.0f, diffuserShare[step][(size_t) c] * range));
            }
            hadamard (x);
            for (int c = 0; c < channels; ++c)
                if (diffuserFlip[step][(size_t) c])
                    x[(size_t) c] = -x[(size_t) c];
        }

        // The tank: modulated lines, a Householder mix, absorption, feedback.
        std::array<float, channels> d;
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
        {
            const float mod = modNow[(size_t) c] + (modNext[(size_t) c] - modNow[(size_t) c]) * along;
            const double delay = tankLength[(size_t) c] + mod + 2.0;
            d[(size_t) c] = tank[(size_t) c].read (delay);
            sum += d[(size_t) c];
        }
        const float householder = sum * (2.0f / channels);
        for (int c = 0; c < channels; ++c)
        {
            auto& state = tankState[(size_t) c];
            const float a = tankSmooth[(size_t) c];
            state = (1.0f - a) * (d[(size_t) c] - householder) + a * state;
            tank[(size_t) c].push (x[(size_t) c] + tankGain[(size_t) c] * state);
        }

        const float tankLeft = 0.5f * (d[0] + d[2] + d[4] + d[6]);
        const float tankRight = 0.5f * (d[1] + d[3] + d[5] + d[7]);

        // --- Shimmer: two crossfading grains read the tank an octave up.
        const float tankMono = 0.5f * (tankLeft + tankRight);
        tankEnergy += energyCoefficient * (tankMono * tankMono - tankEnergy);
        shiftLine.push (tankMono);
        shiftPhase += 1.0 / shiftWindow;   // the read point gains a sample every sample: twice the speed
        shiftPhase -= std::floor (shiftPhase);
        const double other = shiftPhase + 0.5 - std::floor (shiftPhase + 0.5);
        const float w = (float) std::sin (pi * shiftPhase);
        const float shifted = w * w * shiftLine.read (2.0 + shiftWindow * (1.0 - shiftPhase))
                            + (1.0f - w * w) * shiftLine.read (2.0 + shiftWindow * (1.0 - other));
        shimmerHigh += shimmerHighCoefficient * (shifted - shimmerHigh);
        shimmerLow += shimmerLowCoefficient * (shifted - shimmerHigh - shimmerLow);
        // Turned down as the tank gets loud, and off under FREEZE, so it can't run away.
        const float guard = std::min (1.0f, 0.3f / std::sqrt (tankEnergy + 1.0e-12f));
        shimmerInject = softClip (0.8f * now.shimmer * guard * open * shimmerLow);

        // DUCK: the sends' output dips by up to 30 dB as the input rises
        // from −42 to −6 dBFS, and blooms back as it falls away.
        float wetGain = 1.0f;
        if (duckNow > 0.0f || p.duck > 0.0f)
        {
            glide (duckNow, std::clamp (p.duck, 0.0f, 1.0f), fast);
            const float level = std::max (std::abs (inLeft), std::abs (inRight));
            duckEnvelope += (level > duckEnvelope ? duckAttack : duckRelease) * (level - duckEnvelope);
            if (i % 16 == 0)
            {
                const float db = 20.0f * std::log10 (std::max (duckEnvelope, 1.0e-6f));
                duckGain = std::pow (10.0f, -1.5f * duckNow * std::clamp ((db + 42.0f) / 36.0f, 0.0f, 1.0f));
            }
            wetGain = duckGain;
        }

        left[i] = inLeft + wetGain * (now.delayLevel * delayed[0] + now.reverbLevel * outputTrim * tankLeft);
        right[i] = inRight + wetGain * (now.delayLevel * delayed[1] + now.reverbLevel * outputTrim * tankRight);
    }

    if (levelTarget <= 0.0f && now.delayLevel <= 0.0f && now.reverbLevel <= 0.0f)
    {
        active = false;   // both sends closed: back to passing through, clearing the lines a block at a time
        clearPosition = 0;
    }
}

} // namespace silt
