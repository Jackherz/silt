#include <silt/Params.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace silt::params
{
namespace
{
const std::array<Info, count>& table()
{
    static const std::array<Info, count> list { {
#define X(ident, idText, nameText, kindValue, lo, hi, step, mid, def, list, fmt, where) \
        { idText, nameText, Kind::kindValue, (float) (lo), (float) (hi), (float) (step), (float) (mid), (float) (def), list, Format::fmt, Builds::where },
        SILT_PARAMS (X)
#undef X
    } };
    return list;
}

// JUCE's jlimit and roundToInt, to the bit.
float limit (float low, float high, float v) noexcept { return v < low ? low : (high < v ? high : v); }

int roundToInt (double value) noexcept
{
    double n = value + 6755399441055744.0;
    std::int32_t parts[2];
    std::memcpy (parts, &n, sizeof (n));
    return parts[0];   // little-endian (every target Silt builds for)
}

float skewFor (const Info& p) noexcept
{
    return std::log (0.5f) / std::log ((p.centre - p.low) / (p.high - p.low));
}

std::string fixed (double v, int decimals)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, v);
    return buffer;
}

std::string whole (int n) { return std::to_string (n); }

std::string noteName (int note)
{
    if (note < 0 || note > 127)
        return {};
    return std::string (keyNames[note % 12]) + whole (note / 12 - 2);
}

std::string ms (float v)
{
    if (v >= 1000.0f)
        return fixed (v / 1000.0f, 2) + " s";
    return (v < 10.0f ? fixed (v, 1) : whole (roundToInt (v))) + " ms";
}

std::string hz (float v) { return fixed (v, v < 10.0f ? 2 : 1) + " Hz"; }

std::string kHz (float v)
{
    return v >= 1000.0f ? fixed (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : whole (roundToInt (v)) + " Hz";
}

std::string signedPercent (float v)
{
    const int n = roundToInt (v);
    return (n > 0 ? "+" : "") + whole (n) + "%";
}

// "A-B 40%" between named points, the name itself near one.
std::string between (float v, const char* const* names, int last)
{
    const int nearest = roundToInt (v);
    if (std::abs (v - (float) nearest) < 0.02f)
        return names[nearest];
    const int lower = std::min (last - 1, (int) v);
    return std::string (names[lower]) + "-" + names[lower + 1] + " " + whole (roundToInt ((v - (float) lower) * 100.0f)) + "%";
}
} // namespace

const Info& info (int id) noexcept
{
    return table()[(size_t) limit (0.0f, (float) (count - 1), (float) id)];
}

int find (std::string_view id) noexcept
{
    const auto& list = table();
    for (size_t i = 0; i < list.size(); ++i)
        if (id == list[i].id)
            return (int) i;
    return -1;
}

bool inBuild (int id, bool instrument) noexcept
{
    const auto b = info (id).builds;
    return b == Builds::both || (b == Builds::instrument) == instrument;
}

float defaultFor (int id, bool instrument) noexcept
{
    if (id == liveRun && ! instrument)
        return 2.0f;   // Silt FX: the input plays the envelope
    return info (id).defaultValue;
}

float snapValue (int id, float v) noexcept
{
    const auto& p = info (id);
    switch (p.kind)
    {
        case Kind::integer:
        case Kind::choice:
            return (float) roundToInt (limit (p.low, p.high, v));
        case Kind::boolean:
        case Kind::real:
            break;
    }
    if (p.mapping() == Mapping::cubicRate)
        return std::abs (v) < 0.01f ? 0.0f : v;
    if (p.interval > 0.0f)
        v = p.low + p.interval * std::floor ((v - p.low) / p.interval + 0.5f);
    return (v <= p.low || p.high <= p.low) ? p.low : (v >= p.high ? p.high : v);
}

float to01 (int id, float v) noexcept
{
    const auto& p = info (id);
    v = snapValue (id, v);
    switch (p.kind)
    {
        case Kind::integer: return limit (0.0f, 1.0f, limit (0.0f, 1.0f, (v - p.low) / (p.high - p.low)));
        case Kind::choice:  return limit (0.0f, 1.0f, limit (0.0f, 1.0f, v / p.high));
        case Kind::boolean:
        case Kind::real:
            break;
    }
    if (p.mapping() == Mapping::cubicRate)
        return limit (0.0f, 1.0f, 0.5f * (std::cbrt (limit (-50.0f, 50.0f, v) / 50.0f) + 1.0f));
    const float proportion = limit (0.0f, 1.0f, (v - p.low) / (p.high - p.low));
    if (p.mapping() == Mapping::linear)
        return proportion;
    return std::pow (proportion, skewFor (p));
}

float from01 (int id, float proportion) noexcept
{
    return snapValue (id, rangeFrom01 (id, proportion));
}

float rangeFrom01 (int id, float proportion) noexcept
{
    const auto& p = info (id);
    proportion = limit (0.0f, 1.0f, limit (0.0f, 1.0f, proportion));
    float v = 0.0f;
    switch (p.kind)
    {
        case Kind::integer: v = limit (p.low, p.high, proportion * (p.high - p.low) + p.low); break;
        case Kind::choice:  v = limit (0.0f, p.high, proportion * p.high); break;
        case Kind::boolean:
        case Kind::real:
            if (p.mapping() == Mapping::cubicRate)
            {
                const float u = 2.0f * proportion - 1.0f;
                v = 50.0f * u * u * u;
            }
            else
            {
                if (p.mapping() == Mapping::skewed && proportion > 0.0f)
                    proportion = std::exp (std::log (proportion) / skewFor (p));
                v = p.low + (p.high - p.low) * proportion;
            }
            break;
    }
    return v;
}

float hostDefault (int id, bool instrument) noexcept
{
    // JUCE's value tree starts from the range's value for the 0–1 default, not
    // snapped; then, connecting its state, sets the snapped default through the
    // parameter if that's not approximately the same.
    const float default01 = info (id).kind == Kind::boolean ? (defaultFor (id, instrument) > 0.5f ? 1.0f : 0.0f)
                                                             : to01 (id, defaultFor (id, instrument));
    const float start = rangeFrom01 (id, default01);
    const float snapped = from01 (id, default01);
    if (approximatelyEqual (snapped, start))
        return start;
    return from01 (id, to01 (id, from01 (id, to01 (id, snapped))));
}

float hostSet (int id, float value) noexcept
{
    return from01 (id, to01 (id, from01 (id, to01 (id, value))));
}

bool approximatelyEqual (float a, float b) noexcept
{
    if (! (std::isfinite (a) && std::isfinite (b)))
        return a == b;
    const float diff = std::abs (a - b);
    return diff <= std::numeric_limits<float>::min()
        || diff <= std::numeric_limits<float>::epsilon() * std::max (std::abs (a), std::abs (b));
}

bool inPresets (int id) noexcept
{
    return id != monitor && id != captureBars && id != captureFeed && id != sampleGain && id != inputGain && id != root;
}

double quartersFor (int index) noexcept
{
    constexpr int last = (int) std::size (divisionQuarters) - 1;
    return divisionQuarters[index < 0 ? 0 : (index > last ? last : index)];
}

std::string text (int id, float v)
{
    const auto& p = info (id);
    if (p.kind == Kind::boolean)
        return v >= 0.5f ? "On" : "Off";
    if (p.kind == Kind::choice)
    {
        const int i = roundToInt (v);
        return i >= 0 && i < (int) p.choices.size() ? p.choices[(size_t) i] : "";
    }

    switch (p.format)
    {
        case Format::plain:          return whole (roundToInt (v));
        case Format::percent:        return whole (roundToInt (v)) + "%";
        case Format::finePercent:    return fixed (v, 2) + "%";
        case Format::signedPercent:  return signedPercent (v);
        case Format::ms:             return ms (v);
        case Format::seconds:        return ms (v * 1000.0f);
        case Format::hz:             return hz (v);
        case Format::kHz:            return kHz (v);
        case Format::semitones:      return (v >= 0.005f ? "+" : v <= -0.005f ? "" : " ") + fixed (std::abs (v) < 0.005f ? 0.0f : v, 2) + " st";
        case Format::semitonesSpread: return (v < 0.05f ? std::string ("0") : "\xc2\xb1" + fixed (v, 1)) + " st";
        case Format::semitonesDepth: return fixed (v, 1) + " st";
        case Format::times:          return fixed (v, 2) + "x";
        case Format::db:             return v <= -47.95f ? std::string ("-inf dB") : fixed (v, 1) + " dB";
        case Format::sampleGain:     return (v > 0.04f ? "+" : "") + fixed (v, 1) + " dB";
        case Format::note:           return noteName (roundToInt (v));
        case Format::grainCount:     { const int n = roundToInt (v); return whole (n) + (n == 1 ? " grain" : " grains"); }
        case Format::steps:          return whole (roundToInt (v)) + " steps";
        case Format::heads:          { const int n = roundToInt (v); return whole (n) + (n == 1 ? " head" : " heads"); }
        case Format::resTune:
        {
            const int note = roundToInt (v);
            const int cents = roundToInt ((v - (float) note) * 100.0f);
            return noteName (note) + (cents == 0 ? std::string() : (cents > 0 ? " +" : " ") + whole (cents) + "c");
        }
        case Format::resShift:
        {
            const int s = roundToInt (v);
            if (s == 0)
                return "0";
            return (s > 0 ? "+" : "") + (s % 12 == 0 ? whole (s / 12) + " OCT" : whole (s) + " ST");
        }
        case Format::octaves:        return fixed (v, 1) + " oct";
        case Format::oddEven:
        {
            const int n = roundToInt (v);
            return n < 0 ? "ODD " + whole (-n) + "%" : n > 0 ? "EVEN " + whole (n) + "%" : std::string ("ALL");
        }
        case Format::stOffset:
            if (roundToInt (v * 100.0f) == 0)
                return "0 st";
            return (v > 0.0f ? "+" : "") + fixed (v, 2) + " st";
        case Format::peaks:          return fixed (v, 1) + " peaks";
        case Format::perSecond:      return whole (roundToInt (v)) + " /s";
        case Format::morph:
        {
            static const char* names[] { "LP", "BP", "HP" };
            return between (v, names, 2);
        }
        case Format::tone:
        {
            const int n = roundToInt (v);
            return n == 0 ? std::string ("FLAT") : (n < 0 ? "DARK " : "BRIGHT ") + whole (std::abs (n)) + "%";
        }
        case Format::bits:           return v >= 15.995f ? std::string ("OFF") : fixed (v, 1) + " bits";
        case Format::colorRate:      return v >= colorRateOff - 1.0f ? std::string ("OFF") : kHz (v);
        case Format::spaceTone:
        {
            const int n = roundToInt (v);
            return n < 50 ? "DARK " + whole ((50 - n) * 2) + "%" : n > 50 ? "BRIGHT " + whole ((n - 50) * 2) + "%" : std::string ("NATURAL");
        }
        case Format::modRate:        return std::abs (v) < 0.005f ? std::string ("STOP") : (v < 0.0f ? "-" : "") + hz (std::abs (v));
        case Format::waveShape:
        {
            static const char* names[] { "SINE", "TRI", "SAW", "SQUARE" };
            return between (v, names, 3);
        }
        case Format::degrees:        return whole (roundToInt (v)) + "\xc2\xb0";
    }
    return whole (roundToInt (v));
}
} // namespace silt::params
