#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../Core/Node.h"

// Every kind of node the editor can add. A new kind of node is one class plus one line in the factory.
struct NodeTypeInfo
{
    std::string type;
    juce::String label;
    juce::String category;   // the group the Add node menu files it under
};

const std::vector<NodeTypeInfo>& nodeTypes();

// Returns null for an unknown type. `startActive` sets whether the node starts switched on.
std::shared_ptr<Node> createNode (const std::string& type, const std::string& id, const juce::String& name,
                                  ParamRegistry& registry, bool startActive);
