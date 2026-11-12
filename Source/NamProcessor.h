#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "NamMetadata.h"

// Thin wrapper around NeuralAmpModelerCore. All NAM/Eigen headers live in the
// .cpp only, so if the NAM API changes between versions, this is the one file
// pair that needs touching.
class NamProcessor
{
public:
    NamProcessor();
    ~NamProcessor();

    // Loads a .nam file. Returns false and fills `error` on failure
    // (for example, an architecture the linked NAM core does not know).
    bool load (const std::filesystem::path& file, std::string& error);

    // Call before processing, and again whenever sample rate or block size changes.
    void prepare (double sampleRate, int maxBlockSize);

    // Mono in, mono out. `numSamples` must not exceed maxBlockSize.
    void process (const float* input, float* output, int numSamples);

    bool isLoaded() const noexcept;

    // What the file says about itself (loudness, gear type and so on).
    void setMetadata (NamMetadata m) { meta = std::move (m); }
    const NamMetadata& metadata() const noexcept { return meta; }
    double loudnessFor (double size) const noexcept { return meta.loudnessFor (size); }

    // Slimmable (container) models, such as A2 files with several sizes.
    // size runs from 0 to 1; the NAM core picks the submodel from its thresholds.
    bool isSlimmable() const noexcept;
    std::vector<double> slimmableBreakpoints() const;
    void setSlimmableSize (double size);   // audio thread, cheap if unchanged

    // Sample rate the model was trained at, or a negative value if unknown.
    double expectedSampleRate() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    NamMetadata meta;
};
