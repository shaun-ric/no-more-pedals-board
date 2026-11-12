#include "Graph.h"

#include <algorithm>
#include <map>

void Graph::addNode (std::shared_ptr<Node> node)
{
    if (node == nullptr)
        return;

    // Prepared before it joins the list, so it is ready by the time a plan uses it.
    if (sampleRate_.load() > 0.0)
    {
        node->prepare (sampleRate_.load(), blockCapacity.load());
        node->prepareFade (sampleRate_.load(), blockCapacity.load());
    }

    const juce::ScopedLock sl (listLock);

    for (auto& n : nodeList)
        if (n->id() == node->id())
        {
            n = node;
            return;
        }

    nodeList.push_back (std::move (node));
}

void Graph::removeNode (const std::string& id)
{
    const juce::ScopedLock sl (listLock);

    nodeList.erase (std::remove_if (nodeList.begin(), nodeList.end(),
                                    [&id] (const std::shared_ptr<Node>& n) { return n->id() == id; }),
                    nodeList.end());

    edgeList.erase (std::remove_if (edgeList.begin(), edgeList.end(),
                                    [&id] (const EdgeInfo& e) { return e.from == id || e.to == id; }),
                    edgeList.end());
}

bool Graph::hasEdge (const std::string& from, const std::string& to) const
{
    const juce::ScopedLock sl (listLock);

    for (const auto& e : edgeList)
        if (e.from == from && e.to == to)
            return true;

    return false;
}

bool Graph::connect (const std::string& from, const std::string& to, bool enabled)
{
    juce::String fromName, toName;

    {
        const juce::ScopedLock sl (listLock);

        if (from == to)
            return false;

        for (const auto& e : edgeList)
            if (e.from == from && e.to == to)
                return false;

        bool haveFrom = false, haveTo = false;
        for (const auto& n : nodeList)
        {
            if (n->id() == from) { fromName = n->displayName(); haveFrom = true; }
            if (n->id() == to)   { toName = n->displayName();   haveTo = true; }
        }

        if (! haveFrom || ! haveTo)
            return false;
    }

    const std::string group = "edge:" + from + ">" + to;

    EdgeInfo e;
    e.from = from;
    e.to = to;
    e.level = registry.add (group, "level", fromName + " -> " + toName + " level",
                            ParamKind::Number, -60.0f, 6.0f, 0.0f, 0.5f, " dB");
    e.enabled = registry.add (group, "enabled", fromName + " -> " + toName,
                              ParamKind::Toggle, 0.0f, 1.0f, enabled ? 1.0f : 0.0f, 1.0f);

    const juce::ScopedLock sl (listLock);
    edgeList.push_back (e);
    return true;
}

bool Graph::disconnect (const std::string& from, const std::string& to)
{
    const juce::ScopedLock sl (listLock);

    const auto before = edgeList.size();
    edgeList.erase (std::remove_if (edgeList.begin(), edgeList.end(),
                                    [&] (const EdgeInfo& e) { return e.from == from && e.to == to; }),
                    edgeList.end());

    return edgeList.size() != before;
}

bool Graph::commit (juce::String& error)
{
    std::vector<std::shared_ptr<Node>> nodesCopy;
    std::vector<EdgeInfo> edgesCopy;

    {
        const juce::ScopedLock sl (listLock);
        nodesCopy = nodeList;
        edgesCopy = edgeList;
    }

    const size_t n = nodesCopy.size();

    std::map<std::string, int> indexOf;
    for (size_t i = 0; i < n; ++i)
        indexOf[nodesCopy[i]->id()] = (int) i;

    // Order the nodes so every node comes after the nodes that feed it.
    std::vector<int> indegree (n, 0);
    std::vector<std::vector<int>> next (n);

    for (const auto& e : edgesCopy)
    {
        const auto f = indexOf.find (e.from);
        const auto t = indexOf.find (e.to);
        if (f == indexOf.end() || t == indexOf.end())
            continue;

        next[(size_t) f->second].push_back (t->second);
        ++indegree[(size_t) t->second];
    }

    std::vector<int> ready, order;
    for (size_t i = 0; i < n; ++i)
        if (indegree[i] == 0)
            ready.push_back ((int) i);

    while (! ready.empty())
    {
        const int i = ready.back();
        ready.pop_back();
        order.push_back (i);

        for (const int t : next[(size_t) i])
            if (--indegree[(size_t) t] == 0)
                ready.push_back (t);
    }

    if (order.size() != n)
    {
        error = "The connections form a loop, which is not supported.";
        return false;
    }

    auto plan = std::make_shared<Plan>();
    std::vector<int> position (n, 0);

    for (size_t k = 0; k < n; ++k)
    {
        plan->ordered.push_back (nodesCopy[(size_t) order[k]]);
        position[(size_t) order[k]] = (int) k;
    }

    plan->incoming.assign (n, {});

    for (const auto& e : edgesCopy)
    {
        const auto f = indexOf.find (e.from);
        const auto t = indexOf.find (e.to);
        if (f == indexOf.end() || t == indexOf.end())
            continue;

        Plan::E pe;
        pe.from = position[(size_t) f->second];
        pe.to = position[(size_t) t->second];
        pe.level = e.level;
        pe.enabled = e.enabled;
        pe.smoothed = e.smoothed;

        plan->incoming[(size_t) pe.to].push_back ((int) plan->edges.size());
        plan->edges.push_back (pe);
    }

    for (size_t k = 0; k < n; ++k)
        if (plan->ordered[k]->isOutput())
            plan->outputs.push_back ((int) k);

    plan->buffers.resize (n);
    int widest = 1;
    for (size_t k = 0; k < n; ++k)
    {
        plan->buffers[k].setSize (plan->ordered[k]->numChannels(), blockCapacity.load());
        plan->buffers[k].clear();
        widest = juce::jmax (widest, plan->ordered[k]->numChannels());
    }

    plan->delayed.setSize (widest, juce::jmax (1, blockCapacity.load()));
    plan->delayed.clear();

    // ---- Delay compensation ----------------------------------------------------------------------
    // A path through a node that reports latency arrives later than one that does not. Every path into a
    // node is delayed to match the slowest one, and every output is aligned to the slowest output, so
    // parallel paths add up in step and the outputs stay in time with each other. Connections that are
    // switched off still count, so switching one never changes the timing of the others.
    {
        std::vector<int> arrival (n, 0), through (n, 0);   // when the signal reaches a node, and leaves it

        for (size_t k = 0; k < n; ++k)
        {
            int a = 0;
            for (const int e : plan->incoming[k])
                a = juce::jmax (a, through[(size_t) plan->edges[(size_t) e].from]);

            arrival[k] = a;
            through[k] = a + juce::jmax (0, plan->ordered[k]->latencySamples());
        }

        int outputArrival = 0;
        for (const int k : plan->outputs)
            outputArrival = juce::jmax (outputArrival, arrival[(size_t) k]);

        for (const int k : plan->outputs)
        {
            arrival[(size_t) k] = outputArrival;
            through[(size_t) k] = outputArrival;
        }

        for (size_t k = 0; k < n; ++k)
            for (const int e : plan->incoming[k])
            {
                auto& edge = plan->edges[(size_t) e];
                const int delay = arrival[k] - through[(size_t) edge.from];

                if (delay > 0)
                {
                    edge.line = std::make_shared<DelayLine>();
                    edge.line->delay = delay;
                    edge.line->size = delay + 1;
                    edge.line->ring.assign ((size_t) plan->ordered[(size_t) edge.from]->numChannels(),
                                            std::vector<float> ((size_t) edge.line->size, 0.0f));
                }
            }

        compLatency = outputArrival;
    }

    // Tell each node which input feeds it (for calibration against that input's interface level).
    {
        std::vector<int> firstInput (n, -1);
        for (size_t k = 0; k < n; ++k)
        {
            if (plan->ordered[k]->isInput())
                firstInput[k] = (int) k;
            else
                for (const int e : plan->incoming[k])
                {
                    const int src = firstInput[(size_t) plan->edges[(size_t) e].from];
                    if (src >= 0) { firstInput[k] = src; break; }
                }
        }

        for (size_t k = 0; k < n; ++k)
        {
            Param* level = nullptr;
            if (firstInput[k] >= 0)
                level = registry.find (plan->ordered[(size_t) firstInput[k]]->id() + "/interface");
            plan->ordered[k]->setUpstreamInterface (level);
        }
    }

    // Publish. The old plan is kept alive until releaseRetired().
    {
        const juce::ScopedLock sl (planLock);
        std::shared_ptr<Plan> old = current;
        current = plan;
        active.store (plan.get(), std::memory_order_release);

        if (old != nullptr)
            retired.push_back (std::move (old));
    }

    ++planRevision;
    return true;
}

void Graph::releaseRetired()
{
    std::vector<std::shared_ptr<Plan>> doomed;
    {
        const juce::ScopedLock sl (planLock);
        doomed.swap (retired);
    }
    // `doomed` is destroyed here, outside the lock (and so are any nodes only it kept alive).
}

void Graph::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    blockCapacity = maxBlockSize;

    connectionFade = registry.find ("rig/connectionFadeMs");
    blockMidi.ensureSize (4096);

    for (auto& node : nodes())
    {
        node->prepare (sampleRate, maxBlockSize);
        node->prepareFade (sampleRate, maxBlockSize);
    }

    const juce::ScopedLock sl (planLock);
    if (current != nullptr)
        for (size_t k = 0; k < current->buffers.size(); ++k)
        {
            current->buffers[k].setSize (current->ordered[k]->numChannels(), maxBlockSize);
            current->buffers[k].clear();
        }

    if (current != nullptr)
    {
        current->delayed.setSize (juce::jmax (1, current->delayed.getNumChannels()), juce::jmax (1, maxBlockSize));
        current->delayed.clear();
    }
}

void Graph::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples)
{
    Plan* plan = active.load (std::memory_order_acquire);
    if (plan == nullptr)
        return;

    // This block's notes, for the nodes that take them (always drained so the queue never fills).
    blockMidi.clear();
    if (auto* bus = noteBus.load())
        bus->drain (blockMidi);

    for (size_t k = 0; k < plan->ordered.size(); ++k)
    {
        Node& node = *plan->ordered[k];
        auto& buf = plan->buffers[k];

        if (node.acceptsMidi())
            node.setMidiIn (&blockMidi);

        if (buf.getNumSamples() < numSamples)
            return;   // the plan has not been prepared for this block size

        const int channels = node.numChannels();
        buf.clear (0, numSamples);

        if (node.isInput())
        {
            const int inCh = node.deviceInputChannel();
            if (inCh >= 0 && inCh < numIn && in[inCh] != nullptr)
                buf.copyFrom (0, 0, in[inCh], numSamples);
        }
        else
        {
            for (const int e : plan->incoming[k])
            {
                auto& edge = plan->edges[(size_t) e];
                const auto& src = plan->buffers[(size_t) edge.from];
                const int srcChannels = src.getNumChannels();

                // A shorter path is delayed to match the longest one. The delay line runs every block, even
                // while the connection is off, so it is already full when the connection is switched on.
                if (edge.line != nullptr)
                {
                    auto& dl = *edge.line;
                    const int w0 = dl.writePos;

                    for (int ch = 0; ch < srcChannels && ch < (int) dl.ring.size(); ++ch)
                    {
                        auto& ring = dl.ring[(size_t) ch];
                        const float* x = src.getReadPointer (ch);
                        float* y = plan->delayed.getWritePointer (ch);
                        int w = w0;

                        for (int i = 0; i < numSamples; ++i)
                        {
                            ring[(size_t) w] = x[i];
                            if (++w >= dl.size) w = 0;
                            y[i] = ring[(size_t) w];
                        }
                    }

                    dl.writePos = (w0 + numSamples) % dl.size;
                }

                const float target = edge.enabled->on() ? juce::Decibels::decibelsToGain (edge.level->get()) : 0.0f;

                // The edge's gain moves towards its target over about 10 ms, so switching a
                // connection (or changing its level) never clicks.
                float& state = *edge.smoothed;
                if (state < 0.0f)
                    state = target;

                if (state == 0.0f && target == 0.0f)
                    continue;

                float next = state;
                if (next != target)
                {
                    // Full scale in the global connection fade time (10 ms if it is not set), or at once if 0.
                    const Param* fadeParam = connectionFade.load();
                    const double ms = fadeParam != nullptr ? (double) fadeParam->get() : 10.0;
                    const double sr = sampleRate_.load();
                    const float perSample = (ms <= 0.0 || sr <= 0.0) ? 1.0e6f : (float) (1.0 / (0.001 * ms * sr));
                    const float step = perSample * (float) numSamples;
                    next = state < target ? juce::jmin (state + step, target) : juce::jmax (state - step, target);
                }

                // A mono source feeds every channel.
                for (int c = 0; c < channels; ++c)
                {
                    const int sc = juce::jmin (c, srcChannels - 1);
                    const float* in = edge.line != nullptr ? plan->delayed.getReadPointer (sc) : src.getReadPointer (sc);
                    buf.addFromWithRamp (c, 0, in, numSamples, state, next);
                }

                state = next;
            }
        }

        node.run (buf, numSamples);
    }

    for (const int k : plan->outputs)
    {
        const int oc = plan->ordered[(size_t) k]->deviceOutputChannel();
        if (oc >= 0 && oc < numOut && out[oc] != nullptr)
            juce::FloatVectorOperations::add (out[oc], plan->buffers[(size_t) k].getReadPointer (0), numSamples);
    }
}

std::vector<std::shared_ptr<Node>> Graph::nodes() const
{
    const juce::ScopedLock sl (listLock);
    return nodeList;
}

std::vector<Graph::EdgeInfo> Graph::edges() const
{
    const juce::ScopedLock sl (listLock);
    return edgeList;
}

std::shared_ptr<Node> Graph::findNode (const std::string& id) const
{
    const juce::ScopedLock sl (listLock);

    for (const auto& n : nodeList)
        if (n->id() == id)
            return n;

    return nullptr;
}
