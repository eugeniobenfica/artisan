// Engine tests: project format, node graphs and their Lua, the script runtime. No GPU, no window.

#include "artisan/core/Project.h"
#include "artisan/nodes/NodeCompiler.h"
#include "artisan/nodes/NodeLibrary.h"
#include "artisan/runtime/Game.h"
#include "artisan/script/LuaRuntime.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace artisan;

namespace {

int g_failed = 0;
#define CHECK(cond)                                                                                                   \
    do {                                                                                                              \
        if (!(cond)) {                                                                                                \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                                           \
            ++g_failed;                                                                                               \
        }                                                                                                             \
    } while (0)

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

void projectRoundTrip() {
    Project p = starterProject("Test", 640, 360);
    const std::string text = projectToJson(p);
    Project q;
    std::string err;
    CHECK(projectFromJson(text, q, &err));
    CHECK(q.name == "Test");
    CHECK(q.window.width == 640 && q.window.height == 360);
    CHECK(q.scene().entities.size() == p.scene().entities.size());
    CHECK(projectToJson(q) == text); // nothing lost, nothing changed
    const Entity* spinner = q.scene().findByName("Spinner");
    CHECK(spinner && spinner->graph && spinner->graph->nodes.size() == 3 && spinner->graph->links.size() == 3);
    const Entity* player = q.scene().findByName("Player");
    CHECK(player && player->shape && player->shape->kind == ShapeKind::Capsule && player->light);

    Project bad;
    CHECK(!projectFromJson("{not json", bad, &err));
    CHECK(!projectFromJson(R"({"artisan": 999})", bad, &err));
    // Duplicated ids are repaired.
    CHECK(projectFromJson(R"({"scenes":[{"entities":[{"id":1,"name":"a"},{"id":1,"name":"b"}]}]})", q, &err));
    CHECK(q.scene().entities.size() == 2 && q.scene().entities[0].id != q.scene().entities[1].id);
}

void nodeGraphs() {
    NodeGraph g;
    Node& ev = g.add("event.update", 0, 0);
    Node& move = g.add("action.move", 200, 0);
    Node& num = g.add("value.number", 0, 100);
    num.values["value"] = "5";
    CHECK(g.connect({ev.id, "then", move.id, "in"}));
    CHECK(g.connect({num.id, "value", move.id, "dx"}));
    CHECK(!g.connect({ev.id, "then", move.id, "dx"}));   // execution into data
    CHECK(!g.connect({move.id, "then", move.id, "in"})); // into itself
    CHECK(g.connect({num.id, "value", move.id, "dx"}));  // replaces, does not duplicate
    CHECK(g.links.size() == 2);

    const CompiledGraph c = compileGraph(g);
    CHECK(c.warnings.empty());
    CHECK(c.lua.find("function update(self, dt)") != std::string::npos);
    CHECK(c.lua.find("self.x = self.x + 5") != std::string::npos);

    // Typed values never become code.
    Node& log = g.add("action.log", 400, 0);
    log.values["value"] = "\") os.exit() --";
    g.connect({move.id, "then", log.id, "in"});
    const CompiledGraph c2 = compileGraph(g);
    CHECK(c2.lua.find("log(\"\\\") os.exit() --\")") != std::string::npos);

    // Every library node compiles alone and in a chain without errors.
    for (const NodeDef& d : nodeLibrary()) {
        NodeGraph one;
        Node& e = one.add("event.update", 0, 0);
        Node& n = one.add(d.type, 100, 0);
        if (d.input("in")) one.connect({e.id, "then", n.id, "in"});
        const CompiledGraph cg = compileGraph(one);
        LuaRuntime lua;
        Scene s;
        s.add(Entity{});
        Input in;
        lua.bind(&s, &in);
        std::string logged;
        lua.onLog = [&](const std::string& l) { logged += l; };
        CHECK(lua.attach(s.entities[0].id, cg.lua, d.type));
        lua.update(0.016f, 0.016f);
        if (logged.find("error") != std::string::npos) std::printf("  %s: %s\n", d.type.c_str(), logged.c_str());
        CHECK(logged.find("error") == std::string::npos);
    }
}

void scripts() {
    Scene s;
    Entity e;
    e.name = "Hero";
    e.shape = Shape{};
    s.add(e);
    Entity other;
    other.name = "Coin";
    s.add(other);
    Input in;
    LuaRuntime lua;
    lua.bind(&s, &in);
    std::vector<std::string> log;
    lua.onLog = [&](const std::string& l) { log.push_back(l); };
    const EntityId hero = s.entities[0].id;
    CHECK(lua.attach(hero, R"(
        local speed = 100
        count = 0
        function start(self) log("hi", self.name) end
        function update(self, dt)
            count = count + 1
            if input.down("right") then self.x = self.x + speed * dt end
            self.rotation = 45
            self:set_color(1, 0, 0, 1)
            camera.x = 10
            if input.pressed("space") then self:find("Coin"):destroy() end
        end)", "hero.lua"));
    // A second copy has its own globals.
    CHECK(lua.attach(s.entities[1].id, "function update(self) count = (count or 0) + 100 end", "coin.lua"));
    lua.start();
    CHECK(!log.empty() && log[0] == "hi\tHero");
    in.set(Key::Right, true);
    lua.update(0.5f, 0.5f);
    in.endFrame();
    Entity* h = s.find(hero);
    CHECK(h && near(h->position.x, 50.0f) && near(h->rotation, 45.0f));
    CHECK(h && h->shape->color == (Color{1, 0, 0, 1}));
    CHECK(near(s.camera.position.x, 10.0f));
    in.set(Key::Space, true);
    lua.update(0.5f, 1.0f);
    CHECK(s.findByName("Coin") == nullptr);
    CHECK(lua.behaviors() == 1);

    // A runtime error stops that behavior, once, with a message.
    LuaRuntime broken;
    broken.bind(&s, &in);
    int errors = 0;
    broken.onLog = [&](const std::string& l) { errors += l.rfind("error", 0) == 0; };
    CHECK(broken.attach(hero, "function update(self) self.nope = 1 end", "broken.lua"));
    broken.update(0.1f, 0.1f);
    broken.update(0.1f, 0.2f);
    CHECK(errors == 1);
    CHECK(!broken.attach(hero, "function (", "syntax.lua"));
    // No file or process access.
    CHECK(broken.attach(hero, "function start() log(io == nil, os == nil, loadfile == nil) end", "safe.lua"));
    std::string line;
    broken.onLog = [&](const std::string& l) { line = l; };
    broken.start();
    CHECK(line == "true\ttrue\ttrue");
}

void game() {
    Project p = starterProject("Game", 1280, 720);
    std::map<std::string, std::string> files = {{kProjectFile, projectToJson(p)}, {"scripts/player.lua", kStarterPlayerScript}};
    Game g;
    std::string logged;
    g.onLog = [&](const std::string& l) { logged += l + "\n"; };
    std::string err;
    CHECK(g.load([&](const std::string& path, std::vector<uint8_t>& out) {
        auto it = files.find(path);
        if (it == files.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        return true;
    }, &err));
    g.start();
    CHECK(logged.find("Hello from Player") != std::string::npos);
    g.input.set(Key::Right, true);
    for (int i = 0; i < 60; ++i) g.update(1.0f / 60.0f);
    const Entity* player = g.scene.findByName("Player");
    CHECK(player && near(player->position.x, 320.0f, 1.0f));
    const Entity* spinner = g.scene.findByName("Spinner");
    CHECK(spinner && near(spinner->rotation, 90.0f, 0.5f)); // 90 degrees a second, from its graph
    CHECK(logged.find("error") == std::string::npos);
}

} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"project round trip", projectRoundTrip}, {"node graphs", nodeGraphs}, {"scripts", scripts}, {"game", game}};
    for (const auto& [name, fn] : tests) {
        std::printf("%s\n", name);
        fn();
    }
    std::printf(g_failed ? "%d check(s) failed\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
