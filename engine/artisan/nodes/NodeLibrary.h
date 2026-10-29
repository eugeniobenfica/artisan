#pragma once

// Every kind of node, described once: its pins, its category and the Lua it becomes. The editor
// lists them, the compiler emits them; a new node is one entry in NodeLibrary.cpp.

#include <functional>
#include <string>
#include <vector>

namespace artisan {

enum class PinType { Exec, Number, Bool, String, Any };

struct PinDef {
    std::string name;
    PinType type = PinType::Number;
    std::string defaultValue; // for an input with no link
};

struct NodeDef;

// What the compiler gives a node's code: its inputs as Lua expressions, its execution outputs as
// Lua statements (already compiled), a unique prefix for locals.
struct EmitContext {
    std::function<std::string(const std::string& pin)> input;
    std::function<std::string(const std::string& pin)> exec;
    // A value stored on the node that is not a pin (the Lua Code node's code), as typed.
    std::function<std::string(const std::string& name)> value;
    std::string uid;
};

struct NodeDef {
    std::string type;     // "action.move"
    std::string title;    // "Move By"
    std::string category; // "Actions"
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
    // Events start a chain: they become a Lua function ("update") or a condition inside one.
    enum class Kind { Event, Action, Pure } kind = Kind::Action;
    std::string event; // Event nodes: "start" or "update"
    // A node that carries code of its own (values["code"], edited as text): the Lua Code node.
    bool hasCode = false;
    // Action / Event: Lua statements. Pure: the expression for `pin`.
    std::function<std::string(const EmitContext&)> statements;
    std::function<std::string(const EmitContext&, const std::string& pin)> expression;

    const PinDef* input(const std::string& name) const;
    const PinDef* output(const std::string& name) const;
};

const std::vector<NodeDef>& nodeLibrary();
const NodeDef* findNodeDef(const std::string& type);

} // namespace artisan
