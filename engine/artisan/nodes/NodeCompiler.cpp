#include "artisan/nodes/NodeCompiler.h"

#include "artisan/nodes/NodeLibrary.h"

#include <charconv>
#include <cstdio>
#include <set>
#include <sstream>

namespace artisan {

std::string luaQuote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char ch : s) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 32) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\%03d", ch);
                out += buf;
            } else out += static_cast<char>(ch);
        }
    }
    return out + "\"";
}

namespace {

bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    double v = 0;
    const char* end = s.data() + s.size();
    auto [p, ec] = std::from_chars(s.data(), end, v);
    return ec == std::errc() && p == end;
}

// A typed-in value as a Lua literal of its pin's type: never code (a value cannot inject Lua).
std::string literal(const std::string& value, PinType type) {
    switch (type) {
    case PinType::Number: return isNumber(value) ? value : "0";
    case PinType::Bool: return value == "true" || value == "1" ? "true" : "false";
    case PinType::String: return luaQuote(value);
    case PinType::Any: return isNumber(value) ? value : value == "true" || value == "false" ? value : luaQuote(value);
    case PinType::Exec: break;
    }
    return "nil";
}

std::string indent(const std::string& code, int depth = 1) {
    std::istringstream in(code);
    std::string line, out;
    const std::string pad(static_cast<size_t>(depth) * 4, ' ');
    while (std::getline(in, line)) out += (line.empty() ? "" : pad) + line + "\n";
    return out;
}

class Compiler {
public:
    explicit Compiler(const NodeGraph& g) : g_(g) {}

    CompiledGraph run() {
        std::string start, update, hit;
        for (const Node& n : g_.nodes) {
            const NodeDef* def = findNodeDef(n.type);
            if (!def) {
                out_.warnings.push_back("Unknown node \"" + n.type + "\"");
                continue;
            }
            if (def->kind != NodeDef::Kind::Event) continue;
            visiting_.clear();
            std::string code = statements(n, *def);
            (def->event == "start" ? start : def->event == "hit" ? hit : update) += code;
        }
        out_.lua = "-- Generated from a node graph: edit the graph, not this code.\n";
        if (!start.empty()) out_.lua += "function start(self)\n" + indent(start) + "end\n";
        if (!update.empty()) out_.lua += "function update(self, dt)\n" + indent(update) + "end\n";
        if (!hit.empty()) out_.lua += "function hit(self, other)\n" + indent(hit) + "end\n";
        return std::move(out_);
    }

private:
    EmitContext context(const Node& n, const NodeDef& def) {
        EmitContext c;
        c.uid = "n" + std::to_string(n.id);
        c.input = [this, &n, &def](const std::string& pin) { return input(n, def, pin); };
        c.exec = [this, &n](const std::string& pin) { return follow(n, pin); };
        c.value = [&n](const std::string& name) {
            auto it = n.values.find(name);
            return it != n.values.end() ? it->second : std::string();
        };
        return c;
    }

    std::string statements(const Node& n, const NodeDef& def) {
        if (!def.statements) return {};
        return def.statements(context(n, def));
    }

    // The statements an execution output leads to.
    std::string follow(const Node& n, const std::string& pin) {
        std::string code;
        for (const Link* l : g_.linksFrom(n.id, pin)) {
            const Node* next = g_.find(l->toNode);
            const NodeDef* def = next ? findNodeDef(next->type) : nullptr;
            if (!def) continue;
            if (!visiting_.insert(next->id).second) { // a wire loops back: run it once
                out_.warnings.push_back("A loop at \"" + def->title + "\" was cut");
                continue;
            }
            code += statements(*next, *def);
            visiting_.erase(next->id);
        }
        return code;
    }

    std::string input(const Node& n, const NodeDef& def, const std::string& pin) {
        const PinDef* p = def.input(pin);
        if (!p) return "nil";
        if (const Link* l = g_.linkInto(n.id, pin)) {
            const Node* src = g_.find(l->fromNode);
            const NodeDef* sd = src ? findNodeDef(src->type) : nullptr;
            if (sd && sd->expression) {
                if (depth_ > 64) return "nil"; // a data loop
                ++depth_;
                std::string e = sd->expression(context(*src, *sd), l->fromPin);
                --depth_;
                return e;
            }
        }
        auto it = n.values.find(pin);
        return literal(it != n.values.end() ? it->second : p->defaultValue, p->type);
    }

    const NodeGraph& g_;
    CompiledGraph out_;
    std::set<NodeId> visiting_;
    int depth_ = 0;
};

} // namespace

CompiledGraph compileGraph(const NodeGraph& graph) { return Compiler(graph).run(); }

} // namespace artisan
