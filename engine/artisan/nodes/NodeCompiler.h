#pragma once

// Node graph -> Lua. The result defines the same functions a hand-written script does
// (`start(self)`, `update(self, dt)`), so graphs and scripts run the same way and the editor can
// show any graph as code.

#include "artisan/nodes/NodeGraph.h"

#include <string>
#include <vector>

namespace artisan {

struct CompiledGraph {
    std::string lua;
    std::vector<std::string> warnings; // unknown nodes, loops cut short...
};

CompiledGraph compileGraph(const NodeGraph& graph);

// A text value as a Lua string literal (quoted and escaped).
std::string luaQuote(const std::string& s);

} // namespace artisan
