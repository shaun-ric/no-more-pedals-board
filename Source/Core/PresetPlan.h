#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

// Decides which connections are switched on when a scene preset is applied. Kept apart from the rig so it
// can be tested on its own (tools/t7.cpp).
namespace presetplan
{
    struct Edge
    {
        std::string from, to;
        bool listed = false;          // the preset says what this connection does
        bool presetEnabled = false;   // what it says (only if listed)
        bool defaultEnabled = true;   // the connection's own default, from the scene
    };

    // One result per connection, in the same order.
    //
    // A connection the preset lists does what the preset says. One it does not list (added to the scene after
    // the preset was saved) takes its default, with one exception: a connection OUT of a node the preset does
    // not know about is left off if the node it feeds already has a connection switched on. The unknown node
    // is switched off by the preset, and an amp or pedal that is off passes its input through, so that
    // connection would add the dry guitar to a path that already carries a signal (an amp added in parallel).
    // If it is the only way into the node it feeds (a pedal put into the chain), it is switched on, so the
    // chain is not broken.
    inline std::vector<bool> connectionStates (const std::vector<Edge>& edges,
                                               const std::function<bool (const std::string&)>& nodeIsListed)
    {
        std::vector<bool> on (edges.size(), false);
        std::map<std::string, bool> fed;   // node ID -> a connection into it is switched on

        for (size_t i = 0; i < edges.size(); ++i)
            if (edges[i].listed)
            {
                on[i] = edges[i].presetEnabled;
                if (on[i])
                    fed[edges[i].to] = true;
            }

        for (size_t i = 0; i < edges.size(); ++i)
            if (! edges[i].listed)
            {
                bool state = edges[i].defaultEnabled;

                if (state && ! nodeIsListed (edges[i].from) && fed[edges[i].to])
                    state = false;

                on[i] = state;
                if (state)
                    fed[edges[i].to] = true;
            }

        return on;
    }
}
