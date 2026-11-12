#include "NamProcessor.h"

#include "NAM/dsp.h"
#include "NAM/slimmable.h"
#if __has_include("NAM/get_dsp.h")
 #include "NAM/get_dsp.h"
#endif
#if defined(RIGBENCH_NAM_FAST_TANH) && __has_include("NAM/activations.h")
 #include "NAM/activations.h"
#endif

#include <algorithm>
#include <exception>
#include <vector>

struct NamProcessor::Impl
{
    std::unique_ptr<nam::DSP> dsp;
    nam::SlimmableModel* slim = nullptr;   // points into dsp when the model is slimmable
    double appliedSize = -1.0;
    std::vector<NAM_SAMPLE> in, out;   // NAM_SAMPLE is float or double depending on the build
};

NamProcessor::NamProcessor() : impl (std::make_unique<Impl>()) {}
NamProcessor::~NamProcessor() = default;

bool NamProcessor::load (const std::filesystem::path& file, std::string& error)
{
    try
    {
#if defined(RIGBENCH_NAM_FAST_TANH)
        nam::activations::Activation::enable_fast_tanh();
#endif
        auto model = nam::get_dsp (file);

        if (! model)
        {
            error = "NAM core returned no model for this file.";
            return false;
        }

        impl->dsp = std::move (model);
        impl->slim = dynamic_cast<nam::SlimmableModel*> (impl->dsp.get());
        impl->appliedSize = -1.0;
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
    catch (...)
    {
        error = "Unknown error while loading the model.";
        return false;
    }
}

void NamProcessor::prepare (double sampleRate, int maxBlockSize)
{
    impl->in.assign ((size_t) maxBlockSize, NAM_SAMPLE (0));
    impl->out.assign ((size_t) maxBlockSize, NAM_SAMPLE (0));

    if (impl->dsp)
        impl->dsp->Reset (sampleRate, maxBlockSize);
}

void NamProcessor::process (const float* input, float* output, int numSamples)
{
    if (! impl->dsp || (size_t) numSamples > impl->in.size())
    {
        if (input != output)
            std::copy (input, input + numSamples, output);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
        impl->in[(size_t) i] = (NAM_SAMPLE) input[i];

    // Newer NAM core versions take arrays of channel pointers.
    NAM_SAMPLE* inChannels[1]  = { impl->in.data() };
    NAM_SAMPLE* outChannels[1] = { impl->out.data() };
    impl->dsp->process (inChannels, outChannels, numSamples);

    for (int i = 0; i < numSamples; ++i)
        output[i] = (float) impl->out[(size_t) i];
}

bool NamProcessor::isLoaded() const noexcept
{
    return impl->dsp != nullptr;
}

double NamProcessor::expectedSampleRate() const noexcept
{
    return impl->dsp ? impl->dsp->GetExpectedSampleRate() : -1.0;
}

bool NamProcessor::isSlimmable() const noexcept
{
    return impl->slim != nullptr;
}

std::vector<double> NamProcessor::slimmableBreakpoints() const
{
    return impl->slim != nullptr ? impl->slim->GetSlimmableSizeBreakpoints() : std::vector<double>{};
}

void NamProcessor::setSlimmableSize (double size)
{
    if (impl->slim != nullptr && size != impl->appliedSize)
    {
        impl->slim->SetSlimmableSize (size);
        impl->appliedSize = size;
    }
}
