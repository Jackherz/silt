// Native twin of bench.mjs: the same spike, the same scenes, timed the same way,
// so the WebAssembly cost can be read against the C++ it came from.
//   spike-bench <sample.f32> <frames> <sampleRate>   (planar float32: left, then right)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

extern "C"
{
void spike_init (double);
float* spike_sample_begin (int, double);
float* spike_sample_channel (int);
void spike_sample_end();
void spike_scene (int);
void spike_note (int, float, int);
float* spike_process (int);
int spike_active_grains();
}

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf (stderr, "usage: spike-bench sample.f32 frames sampleRate\n");
        return 1;
    }
    const int frames = std::atoi (argv[2]);
    const double sampleRate = std::atof (argv[3]);
    std::vector<float> data ((size_t) frames * 2);
    std::ifstream (argv[1], std::ios::binary).read (reinterpret_cast<char*> (data.data()), (std::streamsize) (data.size() * sizeof (float)));

    const double rate = 48000.0;
    spike_init (rate);
    const auto t0 = std::chrono::steady_clock::now();
    spike_sample_begin (frames, sampleRate);
    std::copy (data.begin(), data.begin() + frames, spike_sample_channel (0));
    std::copy (data.begin() + frames, data.end(), spike_sample_channel (1));
    spike_sample_end();
    std::printf ("analyse: %.1f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());

    for (int scene = 0; scene < 2; ++scene)
    {
        spike_scene (scene);
        const int notes = scene == 0 ? 4 : 16;
        for (int n = 0; n < notes; ++n)
            spike_note (48 + n * 3, 100.0f / 127.0f, 1);
        const int blocks = (int) (4.0 * rate / 128);
        double seconds = 0.0, peak = 0.0;
        int maxGrains = 0;
        for (int b = 0; b < blocks; ++b)
        {
            const auto s = std::chrono::steady_clock::now();
            const float* out = spike_process (128);
            seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - s).count();
            for (int i = 0; i < 256; ++i)
                peak = std::max (peak, (double) std::abs (out[i]));
            maxGrains = std::max (maxGrains, spike_active_grains());
        }
        std::printf ("%s: %.1f%% of real time, peak %.2f, up to %d grains\n",
                     scene == 0 ? "typical" : "worst", 100.0 * seconds / 4.0, peak, maxGrains);
    }
}
