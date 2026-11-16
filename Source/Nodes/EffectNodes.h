#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../Core/Node.h"

// Built-in mono effects. All start inactive, and an inactive effect costs nothing.
// Turning an effect off cuts its tail. Stereo versions come with the first stereo node.

namespace fxdetail
{
    // Coefficient for a one-pole low-pass filter at `hz`.
    inline float onePole (double sr, double hz)
    {
        return (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * hz / sr));
    }

    // A second-order filter section (RBJ cookbook), computed without allocating, so coefficients can
    // be changed from the audio thread. A section set to 0 dB is skipped.
    //
    // A change of setting does not jump: the coefficients move in a straight line to their new values
    // over 10 ms, so changing an EQ while a note rings (a scene preset recalled without muting the
    // outputs, or a pedal sweeping a band) does not click. Switching a band to or from 0 dB glides to or
    // from "no filter" in the same way. Both ends of the glide are stable filters, and every point on a
    // straight line between two stable filters is stable too. `snap` skips the glide (a node that has just
    // been switched on has nothing ringing).
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;        // in use now
        float tb0 = 1.0f, tb1 = 0.0f, tb2 = 0.0f, ta1 = 0.0f, ta2 = 0.0f;   // where they are heading
        float z1 = 0.0f, z2 = 0.0f;
        bool active = false;         // false: the section is skipped
        bool targetActive = false;   // false: it is gliding to "no filter", and is skipped once it gets there
        int rampLeft = 0, rampLength = 480;

        void reset() noexcept { z1 = z2 = 0.0f; }

        void set (double a0, double nb0, double nb1, double nb2, double na1, double na2, bool isActive, bool snap = false) noexcept
        {
            if (isActive)
            {
                tb0 = (float) (nb0 / a0); tb1 = (float) (nb1 / a0); tb2 = (float) (nb2 / a0);
                ta1 = (float) (na1 / a0); ta2 = (float) (na2 / a0);
            }
            else
            {
                tb0 = 1.0f; tb1 = tb2 = ta1 = ta2 = 0.0f;   // no filter
            }

            targetActive = isActive;

            if (snap || (! active && ! isActive))
            {
                if (isActive && ! active)
                    reset();

                b0 = tb0; b1 = tb1; b2 = tb2; a1 = ta1; a2 = ta2;
                active = isActive;
                rampLeft = 0;
                return;
            }

            if (! active)   // coming on: start as "no filter" and glide in
            {
                reset();
                b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f;
                active = true;
            }

            rampLeft = juce::jmax (1, rampLength);
        }

        void setPeaking (double sr, double hz, double q, double dB, bool snap = false) noexcept
        {
            rampLength = (int) (0.010 * sr);
            const bool on = std::abs (dB) > 0.01 && hz < 0.45 * sr;
            const double A = std::pow (10.0, dB / 40.0), w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
            const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            set (1.0 + alpha / A, 1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, -2.0 * c, 1.0 - alpha / A, on, snap);
        }

        void setLowShelf (double sr, double hz, double dB, bool snap = false) noexcept
        {
            rampLength = (int) (0.010 * sr);
            const bool on = std::abs (dB) > 0.01 && hz < 0.45 * sr;
            const double A = std::pow (10.0, dB / 40.0), w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
            const double c = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071), s = 2.0 * std::sqrt (A) * alpha;
            set ((A + 1) + (A - 1) * c + s,
                 A * ((A + 1) - (A - 1) * c + s), 2.0 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
                 -2.0 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s, on, snap);
        }

        void setHighShelf (double sr, double hz, double dB, bool snap = false) noexcept
        {
            rampLength = (int) (0.010 * sr);
            const bool on = std::abs (dB) > 0.01 && hz < 0.45 * sr;
            const double A = std::pow (10.0, dB / 40.0), w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
            const double c = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071), s = 2.0 * std::sqrt (A) * alpha;
            set ((A + 1) - (A - 1) * c + s,
                 A * ((A + 1) + (A - 1) * c + s), -2.0 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
                 2.0 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s, on, snap);
        }

        void process (float* x, int n) noexcept
        {
            if (! active)
                return;

            int i = 0;

            // While gliding, the coefficients take one step towards their targets on every sample.
            for (; i < n && rampLeft > 0; ++i, --rampLeft)
            {
                const float k = 1.0f / (float) rampLeft;
                b0 += (tb0 - b0) * k; b1 += (tb1 - b1) * k; b2 += (tb2 - b2) * k;
                a1 += (ta1 - a1) * k; a2 += (ta2 - a2) * k;

                const float in = x[i];
                const float out = b0 * in + z1;
                z1 = b1 * in - a1 * out + z2;
                z2 = b2 * in - a2 * out;
                x[i] = out;
            }

            if (rampLeft == 0 && ! targetActive)
            {
                active = false;   // arrived at "no filter": the rest of the block passes untouched
                return;
            }

            for (; i < n; ++i)
            {
                const float in = x[i];
                const float out = b0 * in + z1;
                z1 = b1 * in - a1 * out + z2;
                z2 = b2 * in - a2 * out;
                x[i] = out;
            }
        }
    };
}


class GateNode : public Node
{
public:
    GateNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "gate", reg, startActive)
    {
        threshold = addNumber ("threshold", "Threshold", -90.0f, -20.0f, -60.0f, 1.0f, " dB");
    }

    void prepare (double sr, int maxBlock) override
    {
        gate.prepare ({ sr, (juce::uint32) maxBlock, 1 });
        gate.setRatio (10.0f);
        gate.setAttack (1.0f);
        gate.setRelease (80.0f);
        lastThreshold = 1000.0f;   // forces the first update
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        const float t = threshold->get();
        if (t != lastThreshold) { gate.setThreshold (t); lastThreshold = t; }

        float* ch[1] = { b.getWritePointer (0) };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
        gate.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    void onActivate() override { gate.reset(); }

private:
    Param* threshold = nullptr;
    juce::dsp::NoiseGate<float> gate;
    float lastThreshold = 1000.0f;
};

class CompressorNode : public Node
{
public:
    CompressorNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "compressor", reg, startActive)
    {
        threshold = addNumber ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, 0.5f, " dB");
        ratio     = addNumber ("ratio", "Ratio", 1.0f, 20.0f, 3.0f, 0.1f, " :1");
        attack    = addNumber ("attack", "Attack", 1.0f, 100.0f, 10.0f, 1.0f, " ms");
        release   = addNumber ("release", "Release", 10.0f, 1000.0f, 120.0f, 5.0f, " ms");
        makeup    = addNumber ("makeup", "Makeup gain", 0.0f, 24.0f, 0.0f, 0.5f, " dB");
    }

    void prepare (double sr, int maxBlock) override
    {
        comp.prepare ({ sr, (juce::uint32) maxBlock, 1 });
        lastThreshold = lastRatio = lastAttack = lastRelease = -1000.0f;   // forces the first update
        lastMakeup = 1.0f;
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        const float t = threshold->get(), r = ratio->get(), a = attack->get(), rel = release->get();
        if (t != lastThreshold) { comp.setThreshold (t); lastThreshold = t; }
        if (r != lastRatio)     { comp.setRatio (r);     lastRatio = r; }
        if (a != lastAttack)    { comp.setAttack (a);    lastAttack = a; }
        if (rel != lastRelease) { comp.setRelease (rel); lastRelease = rel; }

        float* ch[1] = { b.getWritePointer (0) };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
        comp.process (juce::dsp::ProcessContextReplacing<float> (block));

        const float gain = juce::Decibels::decibelsToGain (makeup->get());
        b.applyGainRamp (0, 0, n, lastMakeup, gain);
        lastMakeup = gain;
    }

    void onActivate() override
    {
        comp.reset();
        lastMakeup = juce::Decibels::decibelsToGain (makeup->get());
    }

private:
    Param *threshold = nullptr, *ratio = nullptr, *attack = nullptr, *release = nullptr, *makeup = nullptr;
    juce::dsp::Compressor<float> comp;
    float lastThreshold = -1000.0f, lastRatio = -1000.0f, lastAttack = -1000.0f, lastRelease = -1000.0f;
    float lastMakeup = 1.0f;
};

class OverdriveNode : public Node
{
public:
    OverdriveNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "overdrive", reg, startActive)
    {
        drive = addNumber ("drive", "Drive", 0.0f, 1.0f, 0.4f, 0.01f);
        tone  = addNumber ("tone", "Tone", 0.0f, 1.0f, 0.5f, 0.01f);
        level = addNumber ("level", "Level", -24.0f, 6.0f, -6.0f, 0.5f, " dB");
    }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        dcA = fxdetail::onePole (sr, 80.0);
        driveGain.reset (sr, 0.03);
        levelGain.reset (sr, 0.03);
        driveGain.setCurrentAndTargetValue (1.0f);
        levelGain.setCurrentAndTargetValue (1.0f);
        dcState = toneState = 0.0f;
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        float* buf = b.getWritePointer (0);

        driveGain.setTargetValue (juce::Decibels::decibelsToGain (drive->get() * 32.0f));
        levelGain.setTargetValue (juce::Decibels::decibelsToGain (level->get()));
        const float toneA = fxdetail::onePole (sampleRate, 1500.0 + (double) tone->get() * 5500.0);

        for (int i = 0; i < n; ++i)
        {
            const float x = buf[i];
            dcState += dcA * (x - dcState);
            const float clipped = std::tanh ((x - dcState) * driveGain.getNextValue());
            toneState += toneA * (clipped - toneState);
            buf[i] = toneState * levelGain.getNextValue();
        }
    }

    void onActivate() override { dcState = toneState = 0.0f; }

private:
    Param *drive = nullptr, *tone = nullptr, *level = nullptr;
    double sampleRate = 48000.0;
    float dcA = 0.0f, dcState = 0.0f, toneState = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveGain, levelGain;
};

class ChorusNode : public Node
{
public:
    ChorusNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "chorus", reg, startActive)
    {
        rate  = addNumber ("rate", "Rate", 0.1f, 5.0f, 0.8f, 0.05f, " Hz");
        depth = addNumber ("depth", "Depth", 0.0f, 1.0f, 0.25f, 0.01f);
        mix   = addNumber ("mix", "Mix", 0.0f, 1.0f, 0.3f, 0.01f);
    }

    void prepare (double sr, int maxBlock) override
    {
        chorus.prepare ({ sr, (juce::uint32) maxBlock, 1 });
        chorus.setCentreDelay (7.0f);
        chorus.setFeedback (0.0f);
        lastRate = lastDepth = lastMix = -1.0f;   // forces the first update
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        const float r = rate->get(), d = depth->get(), m = mix->get();
        if (r != lastRate)  { chorus.setRate (r);   lastRate = r; }
        if (d != lastDepth) { chorus.setDepth (d);  lastDepth = d; }
        if (m != lastMix)   { chorus.setMix (m);    lastMix = m; }

        float* ch[1] = { b.getWritePointer (0) };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
        chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    void onActivate() override { chorus.reset(); }

private:
    Param *rate = nullptr, *depth = nullptr, *mix = nullptr;
    juce::dsp::Chorus<float> chorus;
    float lastRate = -1.0f, lastDepth = -1.0f, lastMix = -1.0f;
};

class DelayNode : public Node
{
public:
    DelayNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "delay", reg, startActive)
    {
        time     = addNumber ("time", "Time", 40.0f, 1000.0f, 380.0f, 1.0f, " ms");
        feedback = addNumber ("feedback", "Feedback", 0.0f, 0.9f, 0.35f, 0.01f);
        mix      = addNumber ("mix", "Mix", 0.0f, 1.0f, 0.25f, 0.01f);
    }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        delayBuf.assign ((size_t) (sr * 2.0), 0.0f);   // 2 seconds, fixed at prepare time
        writePos = 0;
        dampState = 0.0f;
        dampA = fxdetail::onePole (sr, 3500.0);
        delayTime.reset (sr, 0.05);
        delayTime.setCurrentAndTargetValue (time->get());
        lastWet = mix->get();
        lastFb = juce::jlimit (0.0f, 0.95f, feedback->get());
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        float* buf = b.getWritePointer (0);

        delayTime.setTargetValue (juce::jlimit (20.0f, 1500.0f, time->get()));
        const float fb = juce::jlimit (0.0f, 0.95f, feedback->get());
        const float wet = mix->get();
        const int size = (int) delayBuf.size();
        const float msToSamples = (float) sampleRate * 0.001f;

        // Mix and feedback move to a new setting across the block, so a preset change does not click.
        const float wetStep = (wet - lastWet) / (float) juce::jmax (1, n);
        const float fbStep = (fb - lastFb) / (float) juce::jmax (1, n);
        float wetNow = lastWet, fbNow = lastFb;

        for (int i = 0; i < n; ++i)
        {
            wetNow += wetStep;
            fbNow += fbStep;

            float readPos = (float) writePos - delayTime.getNextValue() * msToSamples;
            if (readPos < 0.0f)
                readPos += (float) size;

            const int i0 = juce::jmin ((int) readPos, size - 1);
            const int i1 = (i0 + 1) % size;
            const float frac = readPos - (float) i0;
            const float delayed = delayBuf[(size_t) i0] + frac * (delayBuf[(size_t) i1] - delayBuf[(size_t) i0]);

            dampState += dampA * (delayed - dampState);
            delayBuf[(size_t) writePos] = buf[i] + dampState * fbNow;
            buf[i] += delayed * wetNow;

            if (++writePos >= size)
                writePos = 0;
        }

        lastWet = wet;
        lastFb = fb;
    }

    void onActivate() override
    {
        std::fill (delayBuf.begin(), delayBuf.end(), 0.0f);
        dampState = 0.0f;
        delayTime.setCurrentAndTargetValue (time->get());
        lastWet = mix->get();
        lastFb = juce::jlimit (0.0f, 0.95f, feedback->get());
    }

private:
    Param *time = nullptr, *feedback = nullptr, *mix = nullptr;
    double sampleRate = 48000.0;
    std::vector<float> delayBuf;
    int writePos = 0;
    float dampA = 0.0f, dampState = 0.0f;
    float lastWet = 0.0f, lastFb = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayTime;
};

class ReverbNode : public Node
{
public:
    ReverbNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "reverb", reg, startActive)
    {
        size    = addNumber ("size", "Size", 0.0f, 1.0f, 0.4f, 0.01f);
        damping = addNumber ("damping", "Damping", 0.0f, 1.0f, 0.5f, 0.01f);
        mix     = addNumber ("mix", "Mix", 0.0f, 1.0f, 0.2f, 0.01f);
    }

    void prepare (double sr, int) override
    {
        reverb.setSampleRate (sr);
        reverb.reset();
        lastSize = lastDamp = lastMix = -1.0f;   // forces the first update
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        const float s = size->get(), d = damping->get(), m = mix->get();

        if (s != lastSize || d != lastDamp || m != lastMix)
        {
            juce::Reverb::Parameters p;
            p.roomSize = s;
            p.damping = d;
            p.wetLevel = m * 0.4f;
            p.dryLevel = 0.5f;      // JUCE doubles this, so the dry signal stays at unity
            p.width = 1.0f;
            p.freezeMode = 0.0f;
            reverb.setParameters (p);
            lastSize = s; lastDamp = d; lastMix = m;
        }

        reverb.processMono (b.getWritePointer (0), n);
    }

    void onActivate() override { reverb.reset(); }

private:
    Param *size = nullptr, *damping = nullptr, *mix = nullptr;
    juce::Reverb reverb;
    float lastSize = -1.0f, lastDamp = -1.0f, lastMix = -1.0f;
};


// Bass, mid and treble: a low shelf at 100 Hz, a broad peak at 700 Hz and a high shelf at 3 kHz.
class ToneEqNode : public Node
{
public:
    ToneEqNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "eq3", reg, startActive)
    {
        bass   = addNumber ("bass", "Bass", -15.0f, 15.0f, 0.0f, 0.5f, " dB");
        mid    = addNumber ("mid", "Mid", -15.0f, 15.0f, 0.0f, 0.5f, " dB");
        treble = addNumber ("treble", "Treble", -15.0f, 15.0f, 0.0f, 0.5f, " dB");
    }

    void prepare (double sampleRate, int) override { sr = sampleRate; fresh = true; }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        const float v[3] = { bass->get(), mid->get(), treble->get() };
        const bool snap = fresh;   // just switched on: go straight to the settings, nothing is ringing
        fresh = false;

        if (snap || v[0] != last[0]) { low.setLowShelf (sr, 100.0, v[0], snap);         last[0] = v[0]; }
        if (snap || v[1] != last[1]) { peak.setPeaking (sr, 700.0, 0.7, v[1], snap);    last[1] = v[1]; }
        if (snap || v[2] != last[2]) { high.setHighShelf (sr, 3000.0, v[2], snap);      last[2] = v[2]; }

        float* x = b.getWritePointer (0);
        low.process (x, n);
        peak.process (x, n);
        high.process (x, n);
    }

    void onActivate() override { low.reset(); peak.reset(); high.reset(); fresh = true; }

private:
    Param *bass = nullptr, *mid = nullptr, *treble = nullptr;
    fxdetail::Biquad low, peak, high;
    bool fresh = true;
    double sr = 44100.0;
    float last[3] = { 1000.0f, 1000.0f, 1000.0f };
};

// Ten octave bands from 31 Hz to 16 kHz, each plus or minus 12 dB.
class GraphicEqNode : public Node
{
public:
    static constexpr int numBands = 10;

    GraphicEqNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "eq10", reg, startActive)
    {
        static const char* names[numBands] = { "b31", "b62", "b125", "b250", "b500", "b1k", "b2k", "b4k", "b8k", "b16k" };
        static const char* labels[numBands] = { "31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz" };

        for (int i = 0; i < numBands; ++i)
        {
            band[i] = addNumber (names[i], labels[i], -12.0f, 12.0f, 0.0f, 0.5f, " dB");
            last[i] = 1000.0f;
        }
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        fresh = true;
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        static const double centres[numBands] = { 31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
        float* x = b.getWritePointer (0);
        const bool snap = fresh;
        fresh = false;

        for (int i = 0; i < numBands; ++i)
        {
            const float v = band[i]->get();
            if (snap || v != last[i]) { filter[i].setPeaking (sr, centres[i], 1.414, v, snap); last[i] = v; }
            filter[i].process (x, n);
        }
    }

    void onActivate() override { for (auto& f : filter) f.reset(); fresh = true; }

private:
    Param* band[numBands] = {};
    fxdetail::Biquad filter[numBands];
    bool fresh = true;
    float last[numBands] = {};
    double sr = 44100.0;
};

// A low shelf and a high shelf, each with its own corner frequency and gain.
class ShelfEqNode : public Node
{
public:
    ShelfEqNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "eqshelf", reg, startActive)
    {
        lowFreq  = addNumber ("lowfreq", "Low shelf frequency", 40.0f, 400.0f, 100.0f, 5.0f, " Hz");
        lowGain  = addNumber ("lowgain", "Low shelf gain", -15.0f, 15.0f, 0.0f, 0.5f, " dB");
        highFreq = addNumber ("highfreq", "High shelf frequency", 1000.0f, 12000.0f, 4000.0f, 100.0f, " Hz");
        highGain = addNumber ("highgain", "High shelf gain", -15.0f, 15.0f, 0.0f, 0.5f, " dB");
    }

    void prepare (double sampleRate, int) override { sr = sampleRate; fresh = true; }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        const float lf = lowFreq->get(), lg = lowGain->get(), hf = highFreq->get(), hg = highGain->get();

        const bool snap = fresh;
        fresh = false;

        if (snap || lf != last[0] || lg != last[1]) { low.setLowShelf (sr, lf, lg, snap);   last[0] = lf; last[1] = lg; }
        if (snap || hf != last[2] || hg != last[3]) { high.setHighShelf (sr, hf, hg, snap); last[2] = hf; last[3] = hg; }

        float* x = b.getWritePointer (0);
        low.process (x, n);
        high.process (x, n);
    }

    void onActivate() override { low.reset(); high.reset(); fresh = true; }

private:
    Param *lowFreq = nullptr, *lowGain = nullptr, *highFreq = nullptr, *highGain = nullptr;
    fxdetail::Biquad low, high;
    bool fresh = true;
    float last[4] = { 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f };
    double sr = 44100.0;
};
