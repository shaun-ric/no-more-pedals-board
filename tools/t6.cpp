// The tuner's pitch detector, on guitar-like test tones (a fundamental with strong harmonics, some noise),
// and the EQ filter's glide (no jump in the output when a setting changes, and a stable result).
#include "Core/PitchDetector.h"
#include "Nodes/EffectNodes.h"
#include <cstdio>
#include <cstdlib>

static std::vector<float> tone (double hz, double sr, int n, double level = 0.2)
{
    std::vector<float> x ((size_t) n);
    std::srand (1);
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * 3.14159265358979323846 * hz * (double) i / sr;
        const double noise = ((double) std::rand() / RAND_MAX - 0.5) * 0.02;
        // A second harmonic louder than the fundamental, as a plucked low string often has.
        x[(size_t) i] = (float) (level * (0.6 * std::sin (t) + 0.9 * std::sin (2.0 * t + 0.4) + 0.4 * std::sin (3.0 * t + 1.1) + noise));
    }
    return x;
}

static double centsOff (double measured, double truth) { return 1200.0 * std::log2 (measured / truth); }

int main()
{
    const double sr = 48000.0;
    const int n = 4096;

    // The six open strings, drop D, a 12th-fret high E, and a string 12 cents sharp.
    const double notes[] = { 82.407, 110.0, 146.832, 195.998, 246.942, 329.628, 73.416, 659.255, 110.0 * std::pow (2.0, 12.0 / 1200.0) };
    double worst = 0.0;
    int found = 0;
    for (const double hz : notes)
    {
        const auto x = tone (hz, sr, n);
        const auto r = pitch::detect (x.data(), n, sr);
        if (r.found)
        {
            ++found;
            worst = std::max (worst, std::abs (centsOff (r.hz, hz)));
        }
    }
    std::printf ("pitch: found %d of 9, worst error under 1.5 cents %d\n", found, (int) (worst < 1.5));

    const auto lowE = pitch::nearestNote (82.407);
    const auto sharpA = pitch::nearestNote (110.0 * std::pow (2.0, 12.0 / 1200.0));
    const auto flatG = pitch::nearestNote (195.998 * std::pow (2.0, -30.0 / 1200.0));
    std::printf ("notes: %s%d %s%d %+.0f %s%d %+.0f\n", lowE.name, lowE.octave, sharpA.name, sharpA.octave, sharpA.cents,
                 flatG.name, flatG.octave, flatG.cents);

    // Silence and noise have no pitch.
    std::vector<float> silence ((size_t) n, 0.0f), noise ((size_t) n);
    for (auto& v : noise)
        v = (float) (((double) std::rand() / RAND_MAX - 0.5) * 0.3);
    std::printf ("no pitch: silence %d noise %d\n", (int) ! pitch::detect (silence.data(), n, sr).found,
                 (int) ! pitch::detect (noise.data(), n, sr).found);

    // ---- Smoother ---------------------------------------------------------------------------------------
    // An A string 4 cents flat, read with +-8 cents of jitter and every seventh reading an octave too high (the
    // kind of reading that makes a tuner jump). After settling, the shown pitch must wobble less than half as much
    // as the readings, and never jump to the octave. A new note (D3) must take over after exactly 3 readings.
    {
        pitch::Smoother sm;
        const double truth = 110.0 * std::pow (2.0, -4.0 / 1200.0);
        std::srand (7);
        double worstShown = 0.0, shownSq = 0.0, rawSq = 0.0;
        int counted = 0;
        for (int i = 0; i < 80; ++i)
        {
            const double jitter = ((double) std::rand() / RAND_MAX - 0.5) * 16.0;
            const double reading = (i % 7 == 6) ? truth * 2.0 : truth * std::pow (2.0, jitter / 1200.0);
            const double shown = sm.add (reading);
            if (i >= 10)
            {
                const double e = centsOff (shown, truth);
                worstShown = std::max (worstShown, std::abs (e));
                shownSq += e * e;
                if (i % 7 != 6)
                {
                    rawSq += jitter * jitter;
                    ++counted;
                }
            }
        }
        const double shownRms = std::sqrt (shownSq / 70.0), rawRms = std::sqrt (rawSq / counted);

        const double d3 = 146.832;
        const bool held1 = std::abs (centsOff (sm.add (d3), truth)) < 5.0;
        const bool held2 = std::abs (centsOff (sm.add (d3), truth)) < 5.0;
        const bool moved = std::abs (centsOff (sm.add (d3), d3)) < 0.5;

        int quietTicks = 0;
        while (sm.quiet() > 0.0 && quietTicks < 100)
            ++quietTicks;

        std::printf ("smoother: wobble under half of raw %d, no octave jump %d, new note after 3 readings %d, clears after 9 quiet %d\n",
                     (int) (shownRms < 0.5 * rawRms), (int) (worstShown < 6.0), (int) (held1 && held2 && moved), (int) (quietTicks == 8));
        std::printf ("smoother detail: raw rms %.1f, shown rms %.1f, worst %.1f cents\n", rawRms, shownRms, worstShown);
    }

    // ---- EQ glide ---------------------------------------------------------------------------------------
    // A steady 100 Hz tone through a low shelf that is switched from +12 dB to -12 dB mid-stream. A click shows
    // as a sample-to-sample step larger than any the tone itself makes. With the glide there must be none;
    // without it (the old behaviour, kept as `snap`) there is one, which shows the test can tell them apart.
    // Either way the output must settle at the new gain.
    struct Run { float stepAtChange, stepOfTone, peakAfter; };

    auto run = [sr] (bool snap)
    {
        fxdetail::Biquad f;
        f.setLowShelf (sr, 400.0, 12.0, true);

        std::vector<float> x (9600);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (0.25 * std::sin (2.0 * 3.14159265358979323846 * 100.0 * (double) i / sr));

        f.process (x.data(), 4807);
        f.setLowShelf (sr, 400.0, -12.0, snap);
        f.process (x.data() + 4807, 4793);

        Run r { 0.0f, 0.0f, 0.0f };
        for (size_t i = 1000; i < 4800; ++i)
            r.stepOfTone = std::max (r.stepOfTone, std::abs (x[i] - x[i - 1]));
        for (size_t i = 4808; i < 5400; ++i)
            r.stepAtChange = std::max (r.stepAtChange, std::abs (x[i] - x[i - 1]));
        for (size_t i = 9000; i < 9600; ++i)
            r.peakAfter = std::max (r.peakAfter, std::abs (x[i]));

        return r;
    };

    const auto glide = run (false), jump = run (true);
    std::printf ("eq glide: smooth %d jump-would-click %d settles %d same-end %d\n",
                 (int) (glide.stepAtChange <= glide.stepOfTone * 1.05f), (int) (jump.stepAtChange > jump.stepOfTone * 1.3f),
                 (int) (glide.peakAfter > 0.05f && glide.peakAfter < 0.09f), (int) (std::abs (glide.peakAfter - jump.peakAfter) < 0.002f));

    // Gliding to 0 dB ends with the section switched off, passing the signal untouched.
    fxdetail::Biquad g;
    g.setPeaking (sr, 1000.0, 0.7, 6.0, true);
    std::vector<float> y (2000, 0.1f);
    g.process (y.data(), 500);
    g.setPeaking (sr, 1000.0, 0.7, 0.0);
    g.process (y.data() + 500, 1500);
    std::printf ("eq off: skipped %d untouched %d\n", (int) ! g.active, (int) (std::abs (y[1999] - 0.1f) < 1.0e-6f));
}
