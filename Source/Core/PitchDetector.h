#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Finds the pitch of a single note (a guitar string) in a short stretch of audio, for the tuner. It is the
// YIN method: for every possible period it measures how different the signal is from itself shifted by that
// period, takes the first period where the difference is small, and refines it between samples. Plain C++
// with no allocation after the first call, but it is run on the message thread, never the audio thread.
namespace pitch
{
    struct Result
    {
        bool found = false;
        double hz = 0.0;
    };

    // `n` samples at `sampleRate`. 4096 samples at 48 kHz covers a low B (62 Hz) and above. Signals quieter
    // than about -60 dBFS, and sounds with no clear pitch, give found == false.
    inline Result detect (const float* x, int n, double sampleRate, double minHz = 60.0, double maxHz = 1200.0)
    {
        Result result;

        const int tauMin = (int) (sampleRate / maxHz);
        const int tauMax = (int) (sampleRate / minHz);
        const int window = n - tauMax;

        if (x == nullptr || tauMin < 2 || window < tauMax)
            return result;

        double energy = 0.0;
        for (int i = 0; i < n; ++i)
            energy += (double) x[i] * (double) x[i];

        if (std::sqrt (energy / (double) n) < 0.001)
            return result;

        // The difference for each period, divided by its running average, so small periods are not favoured.
        static thread_local std::vector<float> d;
        d.assign ((size_t) tauMax + 1, 1.0f);

        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const double diff = (double) x[j] - (double) x[j + tau];
                sum += diff * diff;
            }

            running += sum;
            d[(size_t) tau] = running > 0.0 ? (float) (sum * (double) tau / running) : 1.0f;
        }

        // The first dip below the threshold, followed down to its lowest point.
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau])
                    ++tau;

                best = tau;
                break;
            }

        if (best < 0)
        {
            // No clear dip: take the lowest point if it is at least fairly low (a note that is dying away).
            float lowest = 0.35f;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (d[(size_t) tau] < lowest)
                {
                    lowest = d[(size_t) tau];
                    best = tau;
                }
        }

        if (best <= tauMin || best >= tauMax - 1)
            return result;

        // The true lowest point lies between samples: fit a parabola through the three around it.
        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const double denominator = a - 2.0 * b + c;
        const double shift = std::abs (denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0;

        result.found = true;
        result.hz = sampleRate / ((double) best + shift);
        return result;
    }

    struct Note
    {
        const char* name = "";   // "E", "F#", ...
        int octave = 0;          // scientific pitch: the guitar's low E is E2
        double cents = 0.0;      // -50 (flat) to +50 (sharp)
    };

    // The nearest note to a frequency, and how far off it is, with A4 = `a4` Hz.
    inline Note nearestNote (double hz, double a4 = 440.0)
    {
        static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        Note note;
        if (hz <= 0.0)
            return note;

        const double midi = 69.0 + 12.0 * std::log2 (hz / a4);
        const int nearest = (int) std::floor (midi + 0.5);

        note.name = names[((nearest % 12) + 12) % 12];
        note.octave = nearest / 12 - 1;
        note.cents = 100.0 * (midi - (double) nearest);
        return note;
    }

    // Steadies the tuner's reading. Each estimate from detect() goes in, and the pitch to show comes out:
    //  - the median of the last 7 estimates of the same note (about half a second), so a single wild reading (the pick attack, an
    //    octave jump as the string dies away) is ignored rather than averaged in;
    //  - then eased towards, a fifth of the way per estimate, so the needle moves smoothly;
    //  - a different note (more than 50 cents away) is only accepted after 3 estimates agree on it;
    //  - the last reading is held through short gaps, and cleared after `holdTicks` estimates with no pitch.
    class Smoother
    {
    public:
        explicit Smoother (int holdTicksIn = 8) : holdTicks (holdTicksIn) {}

        // One new estimate. Returns the pitch to show (Hz), or 0 for nothing yet.
        double add (double hz)
        {
            quietCount = 0;
            if (hz <= 0.0)
                return shown;

            if (shown > 0.0 && std::abs (centsBetween (hz, shown)) > 50.0)
            {
                // Another note, or a wrong reading: wait until it repeats.
                if (pendingCount > 0 && std::abs (centsBetween (hz, pendingHz)) <= 50.0)
                    ++pendingCount;
                else
                {
                    pendingHz = hz;
                    pendingCount = 1;
                }

                if (pendingCount < 3)
                    return shown;

                count = next = 0;   // start the new note's history afresh
                shown = 0.0;
            }

            pendingCount = 0;
            recent[(size_t) next] = hz;
            next = (next + 1) % (int) recent.size();
            count = std::min (count + 1, (int) recent.size());

            std::array<double, 7> sorted {};
            std::copy (recent.begin(), recent.begin() + count, sorted.begin());
            // With fewer than 7 estimates, only the ones held so far are sorted.
            std::sort (sorted.begin(), sorted.begin() + count);
            const double median = sorted[(size_t) count / 2];

            shown = shown > 0.0 ? shown * std::pow (median / shown, 0.2) : median;
            return shown;
        }

        // No pitch found this time. Returns the pitch to show, or 0 once the note has gone.
        double quiet()
        {
            if (++quietCount > holdTicks)
                reset();
            return shown;
        }

        void reset()
        {
            shown = 0.0;
            count = next = pendingCount = 0;
        }

    private:
        static double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }

        int holdTicks;
        std::array<double, 7> recent {};
        int count = 0, next = 0, quietCount = 0, pendingCount = 0;
        double pendingHz = 0.0, shown = 0.0;
    };
}
