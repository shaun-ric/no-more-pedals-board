#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Node.h"
#include "NoteBus.h"

// A directed graph of nodes. Edges carry audio between nodes and have their own level and on/off
// parameters. Several inputs to a node are summed, and one node can feed several others, so a
// signal can split into parallel paths and rejoin.
//
// The graph can be edited while audio runs. The control thread changes the lists (addNode, removeNode,
// connect, disconnect) and then calls commit(), which builds a new processing plan and publishes it.
// The audio thread only ever reads a complete plan. Old plans are kept until releaseRetired(), which
// the caller invokes once the audio thread can no longer be using them.
class Graph
{
public:
    struct EdgeInfo
    {
        std::string from, to;     // node IDs
        Param* level = nullptr;
        Param* enabled = nullptr;
        std::shared_ptr<float> smoothed = std::make_shared<float> (-1.0f);   // audio-thread gain state; < 0 means "snap to target"
    };

    explicit Graph (ParamRegistry& r) : registry (r) {}

    // ---- Control thread: change the lists, then commit ------------------------------------------
    void addNode (std::shared_ptr<Node> node);                 // a node with the same ID is replaced
    void removeNode (const std::string& id);                   // also removes its edges
    bool hasEdge (const std::string& from, const std::string& to) const;
    bool connect (const std::string& from, const std::string& to, bool enabled = true);
    bool disconnect (const std::string& from, const std::string& to);
    bool commit (juce::String& error);                         // checks for loops, then publishes a new plan
    void releaseRetired();

    // Where notes come from (set once, before audio starts). Optional.
    void setNoteBus (NoteBus* bus) noexcept { noteBus.store (bus); }

    // Device start (message thread): prepares every node and the plan's buffers.
    void prepare (double sampleRate, int maxBlockSize);

    // ---- Audio thread ---------------------------------------------------------------------------
    // Adds the output nodes' signals into `out` (the caller clears it first).
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    // ---- Any thread: snapshots, safe to keep ------------------------------------------------------
    std::vector<std::shared_ptr<Node>> nodes() const;
    std::vector<EdgeInfo> edges() const;
    std::shared_ptr<Node> findNode (const std::string& id) const;

    // Changes every time a plan is published, so a GUI knows to rebuild its views.
    int revision() const noexcept { return planRevision.load(); }

    // The latency, in samples, that every output is aligned to by delay compensation (0 if no node has
    // any). Parallel paths into the same node are delayed to match the slowest, and so are the outputs.
    int compensatedLatency() const noexcept { return compLatency.load(); }

    // Makes views rebuild without changing the plan (after a node was renamed, say).
    void touch() noexcept { ++planRevision; }

private:
    // A signal delayed by a whole number of samples, so parallel paths with different latencies line up.
    struct DelayLine
    {
        std::vector<std::vector<float>> ring;   // one per channel
        int size = 0;                           // delay + 1
        int delay = 0;
        int writePos = 0;
    };

    struct Plan
    {
        struct E
        {
            int from = 0, to = 0;
            Param* level = nullptr;
            Param* enabled = nullptr;
            std::shared_ptr<float> smoothed;
            std::shared_ptr<DelayLine> line;    // set when this path is shorter than the longest one into the same node
        };

        std::vector<std::shared_ptr<Node>> ordered;        // sources first
        std::vector<E> edges;                              // indices into `ordered`
        std::vector<std::vector<int>> incoming;            // per ordered node: indices into `edges`
        std::vector<int> outputs;                          // indices into `ordered`
        std::vector<juce::AudioBuffer<float>> buffers;     // one per ordered node
        juce::AudioBuffer<float> delayed;                  // scratch for a delayed edge
    };

    ParamRegistry& registry;

    mutable juce::CriticalSection listLock;
    std::vector<std::shared_ptr<Node>> nodeList;
    std::vector<EdgeInfo> edgeList;

    std::atomic<double> sampleRate_ { 0.0 };
    std::atomic<int> blockCapacity { 0 };
    std::atomic<Param*> connectionFade { nullptr };   // the global connection fade time, in ms

    juce::CriticalSection planLock;                 // guards `current` and `retired`
    std::shared_ptr<Plan> current;
    std::atomic<Plan*> active { nullptr };          // audio thread
    std::vector<std::shared_ptr<Plan>> retired;
    std::atomic<int> planRevision { 0 };
    std::atomic<int> compLatency { 0 };
    std::atomic<NoteBus*> noteBus { nullptr };
    juce::MidiBuffer blockMidi;
};
