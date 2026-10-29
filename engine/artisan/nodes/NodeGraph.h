#pragma once

// Visual scripting, inspired by Unreal's Blueprints (execution wires and data wires) and
// GameMaker's events: a graph of nodes joined by links. Graphs never run on their own: the
// compiler (NodeCompiler.h) turns them into Lua, so a game has one runtime and a graph can always
// be read as code.

#include <map>
#include <string>
#include <vector>

namespace artisan {

using NodeId = int;

struct Node {
    NodeId id = 0;
    std::string type; // a NodeLibrary definition ("action.move")
    float x = 0.0f, y = 0.0f; // in the graph editor
    // Values typed into inputs that have no link, by pin name (as text: "10", "true", "space").
    std::map<std::string, std::string> values;
};

struct Link {
    NodeId fromNode = 0;
    std::string fromPin; // an output
    NodeId toNode = 0;
    std::string toPin;   // an input
    bool operator==(const Link&) const = default;
};

struct NodeGraph {
    std::vector<Node> nodes;
    std::vector<Link> links;

    Node* find(NodeId id);
    const Node* find(NodeId id) const;
    NodeId nextId() const;
    // The node added. The reference lasts until the next add (the nodes may move): keep its id.
    Node& add(const std::string& type, float x, float y);
    void remove(NodeId id); // and its links
    // The link into an input (inputs take one link), if any.
    const Link* linkInto(NodeId node, const std::string& pin) const;
    // Links out of an output (an execution output drives one node; data outputs feed many).
    std::vector<const Link*> linksFrom(NodeId node, const std::string& pin) const;
    // Adds a link, replacing what was linked into the same input (and, for execution pins, out of
    // the same output). False when the pins do not fit (kinds, direction, a node into itself).
    bool connect(const Link& link);
};

} // namespace artisan
