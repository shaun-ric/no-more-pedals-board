#pragma once

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
}
