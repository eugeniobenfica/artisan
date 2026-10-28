#include "artisan/nodes/NodeGraph.h"

#include "artisan/nodes/NodeLibrary.h"

#include <algorithm>

namespace artisan {

Node* NodeGraph::find(NodeId id) {
    for (Node& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const Node* NodeGraph::find(NodeId id) const { return const_cast<NodeGraph*>(this)->find(id); }

NodeId NodeGraph::nextId() const {
    NodeId id = 1;
    for (const Node& n : nodes) id = std::max(id, n.id + 1);
    return id;
}

Node& NodeGraph::add(const std::string& type, float x, float y) {
    Node n;
    n.id = nextId();
    n.type = type;
    n.x = x;
    n.y = y;
    if (const NodeDef* def = findNodeDef(type))
        for (const PinDef& p : def->inputs)
            if (p.type != PinType::Exec) n.values[p.name] = p.defaultValue;
    nodes.push_back(std::move(n));
    return nodes.back();
}

void NodeGraph::remove(NodeId id) {
    std::erase_if(nodes, [id](const Node& n) { return n.id == id; });
    std::erase_if(links, [id](const Link& l) { return l.fromNode == id || l.toNode == id; });
}

const Link* NodeGraph::linkInto(NodeId node, const std::string& pin) const {
    for (const Link& l : links)
        if (l.toNode == node && l.toPin == pin) return &l;
    return nullptr;
}

std::vector<const Link*> NodeGraph::linksFrom(NodeId node, const std::string& pin) const {
    std::vector<const Link*> out;
    for (const Link& l : links)
        if (l.fromNode == node && l.fromPin == pin) out.push_back(&l);
    return out;
}

bool NodeGraph::connect(const Link& link) {
    const Node* from = find(link.fromNode);
    const Node* to = find(link.toNode);
    if (!from || !to || from == to) return false;
    const NodeDef* fd = findNodeDef(from->type);
    const NodeDef* td = findNodeDef(to->type);
    if (!fd || !td) return false;
    const PinDef* out = fd->output(link.fromPin);
    const PinDef* in = td->input(link.toPin);
    if (!out || !in) return false;
    const bool exec = out->type == PinType::Exec;
    if (exec != (in->type == PinType::Exec)) return false;
    if (!exec && out->type != in->type && out->type != PinType::Any && in->type != PinType::Any &&
        !(in->type == PinType::String)) // anything can be shown as text
        return false;
    std::erase_if(links, [&](const Link& l) {
        return (l.toNode == link.toNode && l.toPin == link.toPin) ||
               (exec && l.fromNode == link.fromNode && l.fromPin == link.fromPin);
    });
    links.push_back(link);
    return true;
}

} // namespace artisan
