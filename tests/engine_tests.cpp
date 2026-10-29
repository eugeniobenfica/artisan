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

    // A scene's own behavior goes and comes back; a scene without one writes no field for it.
    Project withRules = p;
    withRules.scene().script = "scripts/rules.lua";
    withRules.scene().graph = NodeGraph{};
    withRules.scene().graph->add("event.start", 0, 0);
    CHECK(projectToJson(p).find("rules") == std::string::npos);
    CHECK(projectFromJson(projectToJson(withRules), q, &err));
    CHECK(q.scene().script == "scripts/rules.lua" && q.scene().graph && q.scene().graph->nodes.size() == 1);

    Project bad;
    CHECK(!projectFromJson("{not json", bad, &err));
    CHECK(!projectFromJson(R"({"artisan": 999})", bad, &err));
    // Duplicated ids are repaired.
    CHECK(projectFromJson(R"({"scenes":[{"entities":[{"id":1,"name":"a"},{"id":1,"name":"b"}]}]})", q, &err));
    CHECK(q.scene().entities.size() == 2 && q.scene().entities[0].id != q.scene().entities[1].id);
}

void nodeGraphs() {
    NodeGraph g;
    const NodeId ev = g.add("event.update", 0, 0).id;
    const NodeId move = g.add("action.move", 200, 0).id;
    const NodeId num = g.add("value.number", 0, 100).id;
    g.find(num)->values["value"] = "5";
    CHECK(g.connect({ev, "then", move, "in"}));
    CHECK(g.connect({num, "value", move, "dx"}));
    CHECK(!g.connect({ev, "then", move, "dx"}));   // execution into data
    CHECK(!g.connect({move, "then", move, "in"})); // into itself
    CHECK(g.connect({num, "value", move, "dx"}));  // replaces, does not duplicate
    CHECK(g.links.size() == 2);

    const CompiledGraph c = compileGraph(g);
    CHECK(c.warnings.empty());
    CHECK(c.lua.find("function update(self, dt)") != std::string::npos);
    CHECK(c.lua.find("self.x = self.x + 5") != std::string::npos);

    // Typed values never become code.
    const NodeId log = g.add("action.log", 400, 0).id;
    g.find(log)->values["value"] = "\") os.exit() --";
    g.connect({move, "then", log, "in"});
    const CompiledGraph c2 = compileGraph(g);
    CHECK(c2.lua.find("log(\"\\\") os.exit() --\")") != std::string::npos);

    // The Lua Code node runs its code with `a`, `b` and `self`; what it returns goes on.
    {
        NodeGraph lg;
        const NodeId start = lg.add("event.start", 0, 0).id;
        const NodeId code = lg.add("script.lua", 200, 0).id;
        lg.find(code)->values["a"] = "20";
        lg.find(code)->values["code"] = "self.x = a + b\nreturn a * 2";
        const NodeId say = lg.add("action.log", 400, 0).id;
        CHECK(lg.connect({start, "then", code, "in"}));
        CHECK(lg.connect({code, "then", say, "in"}));
        CHECK(lg.connect({code, "result", say, "value"}));
        const NodeId b = lg.add("value.number", 0, 100).id;
        lg.find(b)->values["value"] = "3";
        CHECK(lg.connect({b, "value", code, "b"}));
        const CompiledGraph cg = compileGraph(lg);
        CHECK(cg.warnings.empty());
        LuaRuntime lua;
        Scene s;
        s.add(Entity{});
        Input in;
        lua.bind(&s, &in);
        std::string logged;
        lua.onLog = [&](const std::string& l) { logged += l; };
        CHECK(lua.attach(s.entities[0].id, cg.lua, "lua code"));
        lua.start();
        CHECK(near(s.entities[0].position.x, 23.0f, 0.001f));
        CHECK(logged.find("40") != std::string::npos);
    }

    // Movement as the editor's WASD preset wires it: Input Axis x speed x dt into Move By.
    {
        NodeGraph mg;
        const NodeId update = mg.add("event.update", 0, 0).id;
        const NodeId move = mg.add("action.move", 840, 0).id;
        mg.connect({update, "then", move, "in"});
        const NodeId axis = mg.add("input.axis", 0, 150).id;
        mg.find(axis)->values["negative"] = "a";
        mg.find(axis)->values["positive"] = "d";
        const NodeId fast = mg.add("math.multiply", 280, 150).id;
        mg.find(fast)->values["b"] = "300";
        const NodeId step = mg.add("math.multiply", 560, 150).id;
        CHECK(mg.connect({axis, "value", fast, "a"}));
        CHECK(mg.connect({fast, "result", step, "a"}));
        CHECK(mg.connect({update, "dt", step, "b"}));
        CHECK(mg.connect({step, "result", move, "dx"}));
        const CompiledGraph cg = compileGraph(mg);
        LuaRuntime lua;
        Scene s;
        s.add(Entity{});
        Input in;
        lua.bind(&s, &in);
        std::string logged;
        lua.onLog = [&](const std::string& l) { logged += l; };
        CHECK(lua.attach(s.entities[0].id, cg.lua, "wasd"));
        in.set(Key::D, true);
        lua.update(0.5f, 0.5f);
        if (!near(s.entities[0].position.x, 150.0f, 0.01f)) std::printf("  wasd: x=%f %s\n%s\n", s.entities[0].position.x, logged.c_str(), cg.lua.c_str());
        CHECK(near(s.entities[0].position.x, 150.0f, 0.01f));
    }

    // Every library node compiles alone and in a chain without errors.
    for (const NodeDef& d : nodeLibrary()) {
        NodeGraph one;
        const NodeId e = one.add("event.update", 0, 0).id;
        const NodeId n = one.add(d.type, 100, 0).id;
        if (d.input("in")) one.connect({e, "then", n, "in"});
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

void hitboxes() {
    Scene s;
    Entity floor;
    floor.name = "Floor";
    floor.position = {0, 100};
    floor.hitbox = Hitbox{{400, 40}, {}, true};
    s.add(floor);
    Entity ball;
    ball.name = "Ball";
    ball.position = {0, 75}; // 5 px into the floor (its bottom at 85, the floor's top at 80)
    ball.hitbox = Hitbox{{20, 20}, {0, 0}, false};
    const EntityId ballId = s.add(ball).id;
    Entity coin;
    coin.name = "Coin";
    coin.position = {500, 0};
    coin.hitbox = Hitbox{};
    s.add(coin);

    // The JSON keeps it.
    Scene back;
    CHECK(sceneFromJson(sceneToJson(s), back) && back.entities[1].hitbox && !back.entities[1].hitbox->solid && back.entities[0].hitbox->solid);

    LuaRuntime lua;
    Input in;
    lua.bind(&s, &in);
    std::string logged;
    lua.onLog = [&](const std::string& l) { logged += l + "\n"; };
    CHECK(lua.attach(ballId, "function hit(self, other) log('hit ' .. other.name) end\n"
                             "function update(self, dt) if self:touching('Floor') then log('on floor') end end",
                     "ball"));
    lua.update(0.016f, 0.016f);
    CHECK(near(s.find(ballId)->position.y, 70.0f)); // pushed out, up: resting on the floor
    CHECK(logged.find("hit Floor") != std::string::npos);
    logged.clear();
    lua.update(0.016f, 0.032f); // still touching: hit() is not called again
    CHECK(logged.find("hit") == std::string::npos && logged.find("on floor") != std::string::npos);
    s.find(ballId)->position = {500, 0}; // onto the coin
    lua.update(0.016f, 0.048f);
    CHECK(logged.find("hit Coin") != std::string::npos);
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
    // The scene's behavior: no object as self, the scene's objects through scene.find.
    p.scene().script = "scripts/rules.lua";
    p.scene().graph = NodeGraph{};
    const NodeId ev = p.scene().graph->add("event.start", 0, 0).id;
    const NodeId say = p.scene().graph->add("action.log", 200, 0).id;
    p.scene().graph->find(say)->values["value"] = "Scene graph";
    p.scene().graph->connect({ev, "then", say, "in"});
    std::map<std::string, std::string> files = {{kProjectFile, projectToJson(p)},
                                                {"scripts/player.lua", kStarterPlayerScript},
                                                {"scripts/rules.lua", "function start(self) log('rules', self.x == nil, scene.find('Player').name) end"}};
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
    CHECK(logged.find("rules	true	Player") != std::string::npos);
    CHECK(logged.find("Scene graph") != std::string::npos);
    g.input.set(Key::Right, true);
    for (int i = 0; i < 60; ++i) g.update(1.0f / 60.0f);
    const Entity* player = g.scene.findByName("Player");
    CHECK(player && near(player->position.x, 320.0f, 1.0f));
    const Entity* spinner = g.scene.findByName("Spinner");
    CHECK(spinner && near(spinner->rotation, 90.0f, 0.5f)); // 90 degrees a second, from its graph
    CHECK(logged.find("error") == std::string::npos);

    // The WASD preset as the editor saves it, on an object with a hitbox resting on a solid
    // floor, through the game loop: D moves it right, the floor holds it.
    {
        Project wp;
        Entity mover;
        mover.name = "Mover";
        mover.hitbox = Hitbox{{56, 56}, {}, false};
        NodeGraph mg;
        const NodeId update = mg.add("event.update", 0, 0).id;
        const NodeId move = mg.add("action.move", 840, 0).id;
        mg.connect({update, "then", move, "in"});
        for (int axis = 0; axis < 2; ++axis) {
            const NodeId in = mg.add("input.axis", 0, 150).id;
            mg.find(in)->values["negative"] = axis ? "w" : "a";
            mg.find(in)->values["positive"] = axis ? "s" : "d";
            const NodeId fast = mg.add("math.multiply", 280, 150).id;
            mg.find(fast)->values["b"] = "300";
            const NodeId step = mg.add("math.multiply", 560, 150).id;
            mg.connect({in, "value", fast, "a"});
            mg.connect({fast, "result", step, "a"});
            mg.connect({update, "dt", step, "b"});
            mg.connect({step, "result", move, axis ? "dy" : "dx"});
        }
        mover.graph = mg;
        wp.scene().add(mover);
        Entity floor;
        floor.name = "Floor";
        floor.position = {0, 48};
        floor.hitbox = Hitbox{{2000, 40}, {}, true};
        wp.scene().add(floor);
        const std::string json = projectToJson(wp);
        Game wg;
        std::string wlog;
        wg.onLog = [&](const std::string& l) { wlog += l + "\n"; };
        CHECK(wg.load([&](const std::string& path, std::vector<uint8_t>& out) {
            if (path != kProjectFile) return false;
            out.assign(json.begin(), json.end());
            return true;
        }, &err));
        wg.start();
        wg.input.set(Key::D, true);
        wg.input.set(Key::S, true); // pressing down into the floor
        for (int i = 0; i < 60; ++i) wg.update(1.0f / 60.0f);
        const Entity* m = wg.scene.findByName("Mover");
        CHECK(m && near(m->position.x, 300.0f, 1.0f));
        CHECK(m && near(m->position.y, 0.0f, 0.6f)); // its bottom (28) on the floor's top (28)
        CHECK(wlog.find("error") == std::string::npos);
    }
}

} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"project round trip", projectRoundTrip}, {"node graphs", nodeGraphs}, {"hitboxes", hitboxes}, {"scripts", scripts}, {"game", game}};
    for (const auto& [name, fn] : tests) {
        std::printf("%s\n", name);
        fn();
    }
    std::printf(g_failed ? "%d check(s) failed\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
