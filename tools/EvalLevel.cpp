// Real-music check of the Auto Level estimate: random patches (built like the plugin's Randomize) on
// Free Music Archive tracks, estimate vs. measured K-weighted loudness change.
//
//   g++ -std=c++20 -O2 -Isource -Itools tools/EvalLevel.cpp -o build/tools/EvalLevel -pthread
//   build/tools/EvalLevel <fma_list.csv> [tracks] [patchesPerTrack] [inputLufs]

#define HL_MP3_IMPLEMENTATION
#include "Mp3Decode.h"
#include "dsp/Chain.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

using namespace hl::dsp;

namespace
{
/** BS.1770 K-weighting at any sample rate. */
struct KWeight
{
    double b[2][3], a[2][2], z[2][2][2] {};

    explicit KWeight (double rate)
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / rate), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            b[0][0] = (Vh + Vb * K / Q + K * K) / a0; b[0][1] = 2.0 * (K * K - Vh) / a0; b[0][2] = (Vh - Vb * K / Q + K * K) / a0;
            a[0][0] = 2.0 * (K * K - 1.0) / a0; a[0][1] = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / rate);
            const double a0 = 1.0 + K / Q + K * K;
            b[1][0] = 1.0; b[1][1] = -2.0; b[1][2] = 1.0;
            a[1][0] = 2.0 * (K * K - 1.0) / a0; a[1][1] = (1.0 - K / Q + K * K) / a0;
        }
    }

    /** K-weighted power of a stereo signal from sample `from`. */
    double power (const std::vector<float>& l, const std::vector<float>& r, size_t from)
    {
        double sum = 0.0;
        const std::vector<float>* chans[2] = { &l, &r };

        for (int ch = 0; ch < 2; ++ch)
        {
            double s[2][2] = {};

            for (size_t i = 0; i < chans[ch]->size(); ++i)
            {
                double x = (*chans[ch])[i];

                for (int st = 0; st < 2; ++st)
                {
                    const double y = b[st][0] * x + s[st][0];
                    s[st][0] = b[st][1] * x - a[st][0] * y + s[st][1];
                    s[st][1] = b[st][2] * x - a[st][1] * y;
                    x = y;
                }

                if (i >= from)
                    sum += x * x;
            }
        }

        return sum;
    }
};

/** Mirrors HollowAudioProcessor::randomize() in DSP terms. */
ChainSettings randomPatch (Random& r, std::string& description)
{
    const auto chance = [&r] (float p) { return r.nextFloat() < p; };
    const auto between = [&r] (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); };
    const auto pick = [&r] (std::initializer_list<int> items) { return *(items.begin() + (int) (r.nextInt() % items.size())); };

    ChainSettings s;
    s.enabled.fill (false);
    s.enabled[moduleTrash] = true;
    std::vector<int> others;

    for (int m = 0; m < numModules; ++m)
        if (m != moduleTrash)
            others.push_back (m);

    for (size_t i = others.size() - 1; i > 0; --i)
        std::swap (others[i], others[(size_t) (r.nextInt() % (i + 1))]);

    const int extra = 2 + (int) (r.nextInt() % 3);

    for (int i = 0; i < extra; ++i)
        s.enabled[(size_t) others[(size_t) i]] = true;

    s.trash.numBands = chance (0.55f) ? 1 : (chance (0.5f) ? 2 : 3);
    s.trash.crossovers = { between (120.0f, 600.0f), between (1500.0f, 6000.0f) };

    for (int b = 0; b < s.trash.numBands; ++b)
    {
        auto& band = s.trash.bands[(size_t) b];
        band.algoA = (int) (r.nextInt() % kNumAlgos);
        band.algoB = (int) (r.nextInt() % kNumAlgos);
        band.morph = chance (0.5f) ? 0.0f : between (0.0f, 1.0f);
        band.driveDb = between (4.0f, 34.0f);
        band.bias = chance (0.3f) ? between (-0.6f, 0.6f) : 0.0f;
        band.tone = chance (0.5f) ? 1.0f : between (0.4f, 1.0f);
        band.mix = between (0.5f, 1.0f);
    }

    for (int f = 0; f < 2; ++f)
    {
        auto& fs = s.filters[(size_t) f];
        fs.type = f == 0 ? pick ({ 2, 3, 4, 7, 9 }) : pick ({ 0, 1, 4, 5, 6, 8, 9 });
        fs.cutoff = f == 0 ? between (60.0f, 900.0f) : between (500.0f, 9000.0f);
        fs.reso = between (0.05f, 0.75f);
        fs.driveDb = chance (0.4f) ? between (0.0f, 12.0f) : 0.0f;
    }

    s.convolve = { (int) (r.nextInt() % kNumImpulses), between (0.5f, 1.6f), between (0.0f, 0.8f), chance (0.15f), between (0.25f, 0.9f) };
    s.motion = { (int) (r.nextInt() % kNumMotionModes), between (0.05f, 6.0f), between (0.2f, 0.9f), between (-0.6f, 0.7f),
                 between (40.0f, 1500.0f), between (0.0f, 1.0f), between (0.25f, 0.8f) };
    s.degrade = { between (0.0f, 0.7f), between (0.0f, 0.7f), between (0.0f, 0.7f), between (0.0f, 0.35f),
                  chance (0.4f) ? between (0.0f, 0.5f) : 0.0f, chance (0.3f) ? between (0.0f, 0.5f) : 0.0f,
                  chance (0.3f) ? between (0.1f, 0.6f) : 0.0f, 1.0f };
    s.dynamics.thresholdDb = between (-36.0f, -10.0f);
    s.dynamics.ratio = between (2.0f, 10.0f);
    s.dynamics.gateDb = chance (0.25f) ? between (-60.0f, -30.0f) : -80.0f;
    s.echo = { between (60.0f, 700.0f), between (0.2f, 0.85f), between (-0.7f, 0.3f), between (0.0f, 0.7f),
               between (0.0f, 0.6f), chance (0.4f), between (0.15f, 0.45f) };

    for (auto& l : s.lfos)
    {
        l.shape = (int) (r.nextInt() % kNumLfoShapes);
        l.rateHz = between (0.05f, 4.0f);
    }

    const int routes = 1 + (int) (r.nextInt() % 3);

    for (int i = 0; i < routes; ++i)
        s.slots[(size_t) i] = { 1 + (int) (r.nextInt() % 3), 1 + (int) (r.nextInt() % (numDestinations - 4)), (chance (0.5f) ? -1.0f : 1.0f) * between (0.1f, 0.45f) };

    for (int swaps = (int) (r.nextInt() % 4); swaps > 0; --swaps)
        std::swap (s.order[r.nextInt() % numModules], s.order[r.nextInt() % numModules]);

    s.autoLevel = false;
    s.clipGuard = false;

    for (int id : s.order)
        if (s.enabled[(size_t) id])
        {
            description += description.empty() ? "" : " > ";
            description += moduleName (id);

            if (id == moduleTrash)
                description += std::string (" (") + algoInfo (s.trash.bands[0].algoA).name + ")";
            else if (id == moduleFilter1 || id == moduleFilter2)
                description += std::string (" (") + filterTypeNames()[s.filters[id == moduleFilter1 ? 0 : 1].type] + ")";
            else if (id == moduleConvolve)
                description += std::string (" (") + impulseInfo (s.convolve.impulse).name + ")";
            else if (id == moduleMotion)
                description += std::string (" (") + motionModeNames()[s.motion.mode] + ")";
        }

    return s;
}

struct Result
{
    double error, measured, estimated;
    std::string patch, track;
    ChainSettings settings;
    int trackIndex = 0;
};

/** Renders `settings` with only the first `stages` enabled modules (in chain order) switched on. */
ChainSettings firstStages (const ChainSettings& settings, int stages)
{
    auto s = settings;
    int count = 0;

    for (int id : s.order)
        if (s.enabled[(size_t) id] && ++count > stages)
            s.enabled[(size_t) id] = false;

    return s;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: EvalLevel <fma_list.csv> [tracks] [patchesPerTrack] [inputLufs]\n");
        return 1;
    }

    const int numTracks = argc > 2 ? std::atoi (argv[2]) : 40;
    const int patchesPerTrack = argc > 3 ? std::atoi (argv[3]) : 10;
    const double inputDb = argc > 4 ? std::atof (argv[4]) : -18.0;

    std::vector<std::string> files;
    std::ifstream list (argv[1]);

    for (std::string line; std::getline (list, line);)
        if (! line.empty())
            files.push_back (line.substr (0, line.find (',')));

    std::vector<std::string> chosen;

    for (int i = 0; i < numTracks && ! files.empty(); ++i)
        chosen.push_back (files[(size_t) ((i * 7919 + 13) % (int) files.size())]);

    std::vector<Result> results;
    std::mutex mutex;
    std::atomic<int> next { 0 };

    const auto worker = [&]
    {
        for (int t = next++; t < (int) chosen.size(); t = next++)
        {
            hl::tools::StereoAudio audio;

            if (! hl::tools::decodeMp3 (chosen[(size_t) t], audio) || audio.left.size() < (size_t) (audio.sampleRate * 14.0))
                continue;

            const double rate = audio.sampleRate;
            const size_t start = (size_t) (rate * 2.0), len = (size_t) (rate * 12.0);
            std::vector<float> inL (audio.left.begin() + (long) start, audio.left.begin() + (long) (start + len));
            std::vector<float> inR (audio.right.begin() + (long) start, audio.right.begin() + (long) (start + len));

            KWeight kw (rate);
            const double inPower = kw.power (inL, inR, 0) / (2.0 * (double) len);
            const float norm = (float) std::sqrt (std::pow (10.0, inputDb / 10.0) / std::max (1.0e-12, inPower));

            for (auto* ch : { &inL, &inR })
                for (auto& v : *ch)
                    v *= norm;

            Random rng ((uint32_t) (t * 977 + 5));

            for (int p = 0; p < patchesPerTrack; ++p)
            {
                std::string description;
                const auto settings = randomPatch (rng, description);
                Chain chain;
                chain.setSettings (settings);
                chain.prepare (rate, 512);
                chain.getConvolve().settleForTesting();

                auto l = inL, r = inR;
                TransportInfo transport;
                transport.playing = true;

                for (size_t s = 0; s < len; s += 512)
                {
                    const int n = (int) std::min ((size_t) 512, len - s);
                    chain.setSettings (settings);
                    chain.process (l.data() + s, r.data() + s, n, transport);
                    transport.ppq += n / rate * 2.0;
                }

                const size_t from = (size_t) (rate * 2.0);
                const double measured = 10.0 * std::log10 (std::max (1.0e-20, KWeight (rate).power (l, r, from)) / KWeight (rate).power (inL, inR, from));
                const double estimated = chain.estimateLevelChange();
                std::lock_guard<std::mutex> lock (mutex);
                results.push_back ({ estimated - measured, measured, estimated, description, chosen[(size_t) t].substr (chosen[(size_t) t].rfind ('/') + 1), settings, t });
            }
        }
    };

    std::vector<std::thread> threads;

    for (unsigned i = 0; i < std::max (1u, std::thread::hardware_concurrency()); ++i)
        threads.emplace_back (worker);

    for (auto& th : threads)
        th.join();

    std::vector<double> absErr, measuredAbs;

    for (const auto& r : results)
    {
        absErr.push_back (std::abs (r.error));
        measuredAbs.push_back (std::abs (r.measured));
    }

    std::sort (absErr.begin(), absErr.end());
    std::sort (measuredAbs.begin(), measuredAbs.end());
    const auto pct = [] (const std::vector<double>& v, double p) { return v.empty() ? 0.0 : v[(size_t) std::min ((double) v.size() - 1, p * (double) v.size())]; };
    const auto within = [&] (double db) { return 100.0 * (double) std::count_if (absErr.begin(), absErr.end(), [db] (double e) { return e <= db; }) / (double) std::max<size_t> (1, absErr.size()); };

    std::printf ("%zu renders (%d tracks x %d random patches, input %.0f LUFS)\n\n", results.size(), numTracks, patchesPerTrack, inputDb);
    std::printf ("without auto level: |level change| median %.1f dB, 90%% %.1f dB, max %.1f dB\n", pct (measuredAbs, 0.5), pct (measuredAbs, 0.9), measuredAbs.empty() ? 0.0 : measuredAbs.back());
    std::printf ("with auto level:    |error|        median %.1f dB, 90%% %.1f dB, max %.1f dB\n", pct (absErr, 0.5), pct (absErr, 0.9), absErr.empty() ? 0.0 : absErr.back());
    std::printf ("within 2 dB: %.0f%%   within 3 dB: %.0f%%   within 6 dB: %.0f%%\n\n", within (2.0), within (3.0), within (6.0));

    std::sort (results.begin(), results.end(), [] (const Result& a, const Result& b) { return std::abs (a.error) > std::abs (b.error); });
    std::printf ("worst:\n");

    for (size_t i = 0; i < std::min<size_t> (15, results.size()); ++i)
        std::printf ("  %+6.1f dB (measured %+6.1f, estimated %+6.1f)  %s  [%s]\n", results[i].error, results[i].measured, results[i].estimated,
                     results[i].patch.c_str(), results[i].track.c_str());

    // Stage-by-stage trace of the worst few: where does the estimate go wrong?
    const bool trace = argc > 5 && std::string (argv[5]) == "--trace";

    for (size_t w = 0; trace && w < std::min<size_t> (6, results.size()); ++w)
    {
        const auto& res = results[w];
        hl::tools::StereoAudio audio;
        hl::tools::decodeMp3 (chosen[(size_t) res.trackIndex], audio);
        const double rate = audio.sampleRate;
        const size_t start = (size_t) (rate * 2.0), len = (size_t) (rate * 12.0);
        std::vector<float> inL (audio.left.begin() + (long) start, audio.left.begin() + (long) (start + len));
        std::vector<float> inR (audio.right.begin() + (long) start, audio.right.begin() + (long) (start + len));
        KWeight kw (rate);
        const float norm = (float) std::sqrt (std::pow (10.0, inputDb / 10.0) / (kw.power (inL, inR, 0) / (2.0 * (double) len)));

        for (auto* ch : { &inL, &inR })
            for (auto& v : *ch)
                v *= norm;

        std::printf ("\ntrace %s  [%s]\n", res.patch.c_str(), res.track.c_str());
        const auto& ts = res.settings.trash;
        std::printf ("   trash: %d band(s), x %.0f/%.0f, autogain %d\n", ts.numBands, ts.crossovers[0], ts.crossovers[1], ts.autoGain ? 1 : 0);

        for (int b = 0; b < ts.numBands; ++b)
            std::printf ("     band %d: %s/%s morph %.2f drive %.1f bias %.2f tone %.2f mix %.2f level %.1f\n", b, algoInfo (ts.bands[(size_t) b].algoA).name,
                         algoInfo (ts.bands[(size_t) b].algoB).name, ts.bands[(size_t) b].morph, ts.bands[(size_t) b].driveDb, ts.bands[(size_t) b].bias,
                         ts.bands[(size_t) b].tone, ts.bands[(size_t) b].mix, ts.bands[(size_t) b].levelDb);

        for (int f = 0; f < 2; ++f)
            std::printf ("   filter %d: %s %.0f Hz reso %.2f drive %.1f mix %.2f\n", f + 1, filterTypeNames()[res.settings.filters[(size_t) f].type],
                         res.settings.filters[(size_t) f].cutoff, res.settings.filters[(size_t) f].reso, res.settings.filters[(size_t) f].driveDb, res.settings.filters[(size_t) f].mix);
        int enabled = 0;

        for (int id : res.settings.order)
            enabled += res.settings.enabled[(size_t) id] ? 1 : 0;

        for (int st = 1; st <= enabled; ++st)
        {
            const auto partial = firstStages (res.settings, st);
            Chain chain;
            chain.setSettings (partial);
            chain.prepare (rate, 512);
            chain.getConvolve().settleForTesting();
            auto l = inL, r = inR;
            TransportInfo transport;
            transport.playing = true;

            for (size_t s0 = 0; s0 < len; s0 += 512)
            {
                chain.setSettings (partial);
                chain.process (l.data() + s0, r.data() + s0, (int) std::min ((size_t) 512, len - s0), transport);
            }

            const size_t from = (size_t) (rate * 2.0);
            const double measured = 10.0 * std::log10 (std::max (1.0e-20, KWeight (rate).power (l, r, from)) / KWeight (rate).power (inL, inR, from));
            int count = 0, last = 0;

            for (int id : partial.order)
                if (partial.enabled[(size_t) id] && ++count == st)
                    last = id;

            std::printf ("   after %-10s measured %+7.1f  estimated %+7.1f\n", moduleName (last), measured, chain.estimateLevelChange());
        }
    }

    return 0;
}
