// Plays silt-golden's scene logs through the browser's bridge (SiltWeb.cpp),
// natively, and compares with the plug-in's renders. replay.mjs does the same
// through the WebAssembly build. Native matching the plug-in shows the bridge
// is faithful; the WebAssembly run then shows what the browser's maths changes.
//
//   replay <golden-folder> <sample.f32 planar> <frames> <instrument 0|1> [out-folder]

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <silt/Params.h>

extern "C"
{
void silt_create (int);
void silt_prepare (double, int);
void silt_set_param (int, float);
int silt_set_route (int, int, float, int);
float* silt_sample_begin (int, double);
float* silt_sample_channel (int);
void silt_sample_end();
void silt_midi (int, int, int, int);
void silt_transport (int, double, double, int, int);
float* silt_input();
float* silt_process (int, int);
}

namespace
{
constexpr double rate = 48000.0;

float testInput (long long i, int channel)   // as silt-golden's
{
    const double t = (double) i / rate;
    const double chirp = std::sin (6.283185307179586 * (110.0 * t + 40.0 * t * t)) * 0.3;
    const double pulse = std::fmod (t, 0.5) < 0.05 ? 0.4 * std::sin (6.283185307179586 * 880.0 * t) : 0.0;
    return (float) (chirp + pulse) * (channel == 0 ? 1.0f : 0.8f);
}

struct Line { std::string kind; int sample; std::vector<std::string> args; };
} // namespace

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: replay golden-folder sample.f32 frames instrument [out-folder]\n");
        return 2;
    }
   #if defined (__aarch64__)
    if (std::getenv ("SILT_FTZ") != nullptr)   // as the plug-in's ScopedNoDenormals
    {
        std::uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r" (fpcr));
        fpcr |= (1ull << 24);
        asm volatile ("msr fpcr, %0" : : "r" (fpcr));
    }
   #endif
    const std::string folder = argv[1];
    const int frames = std::atoi (argv[3]);
    const bool instrument = std::atoi (argv[4]) != 0;
    std::vector<float> sample ((size_t) frames * 2);
    std::ifstream (argv[2], std::ios::binary).read (reinterpret_cast<char*> (sample.data()), (std::streamsize) (sample.size() * sizeof (float)));

    const char* names[] { "pitch-chord-sustain", "worst-case", "chain-and-switches", "modulation", "midi-sources-and-follow",
                          "synced-host", "grains-mode-region", "slice", "live-input", "feed" };
    bool allSame = true;
    for (const auto* name : names)
    {
        std::ifstream logFile (folder + "/" + name + ".log");
        std::vector<Line> lines;
        std::string text, kind;
        int total = 0, blockSize = 128, input = 0, playing = 0, hasInput = 0;
        while (std::getline (logFile, text))
        {
            std::istringstream in (text);
            Line l;
            in >> l.kind;
            if (l.kind == "scene")
            {
                std::string sceneName;
                in >> sceneName >> total >> blockSize >> input >> playing >> hasInput;
                continue;
            }
            in >> l.sample;
            for (std::string a; in >> a;)
                l.args.push_back (a);
            lines.push_back (l);
        }

        silt_create (instrument ? 1 : 0);
        silt_prepare (rate, 512);
        if (instrument)
        {
            silt_sample_begin (frames, rate);
            std::memcpy (silt_sample_channel (0), sample.data(), (size_t) frames * sizeof (float));
            std::memcpy (silt_sample_channel (1), sample.data() + frames, (size_t) frames * sizeof (float));
            silt_sample_end();
        }

        std::vector<float> out ((size_t) total * 2);
        size_t next = 0;
        for (int start = 0; start < total; start += blockSize)
        {
            const int n = std::min (blockSize, total - start);
            std::vector<Line> midi;
            for (; next < lines.size() && lines[next].sample < start + n; ++next)
            {
                const auto& l = lines[next];
                if (l.kind == "set")
                    silt_set_param (silt::params::find (l.args[0]), std::stof (l.args[1]));
                else if (l.kind == "route")
                    silt_set_route (std::stoi (l.args[0]), silt::params::find (l.args[1]), std::stof (l.args[2]), std::stoi (l.args[3]));
                else
                    midi.push_back (l);
            }
            for (int sub = 0; sub < n; sub += 128)
            {
                const int m = std::min (128, n - sub);
                for (const auto& e : midi)
                    if (e.sample >= start + sub && e.sample < start + sub + m)
                        silt_midi (e.sample - start - sub, std::stoi (e.args[0]), std::stoi (e.args[1]), std::stoi (e.args[2]));
                silt_transport (playing, (double) (start + sub) / rate * 120.0 / 60.0, 120.0, 4, 4);
                float* in = silt_input();
                for (int i = 0; i < m; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        in[ch * 128 + i] = input ? testInput (start + sub + i, ch) : 0.0f;
                const float* o = silt_process (m, hasInput ? 2 : 0);
                for (int i = 0; i < m; ++i)
                {
                    out[(size_t) (start + sub + i) * 2] = o[i];
                    out[(size_t) (start + sub + i) * 2 + 1] = o[128 + i];
                }
            }
        }

        std::vector<float> ref (out.size());
        std::ifstream (folder + "/" + name + ".f32", std::ios::binary).read (reinterpret_cast<char*> (ref.data()), (std::streamsize) (ref.size() * sizeof (float)));
        double worst = 0.0;
        long long first = -1;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::memcmp (&out[i], &ref[i], sizeof (float)) != 0)
            {
                if (first < 0)
                    first = (long long) i;
                worst = std::max (worst, (double) std::abs (out[i] - ref[i]));
            }
        if (first < 0)
            std::printf ("%-26s identical\n", name);
        else
        {
            std::printf ("%-26s DIFFERS from %.4f s, by up to %g\n", name, (double) first / 2.0 / rate, worst);
            allSame = false;
        }
        if (argc > 5)
            std::ofstream (std::string (argv[5]) + "/" + name + ".f32", std::ios::binary)
                .write (reinterpret_cast<const char*> (out.data()), (std::streamsize) (out.size() * sizeof (float)));
    }
    return allSame ? 0 : 1;
}
