#include "artisan/core/Project.h"

#include <nlohmann/json.hpp>

#include <cstdio>

namespace artisan {

using json = nlohmann::ordered_json;

namespace {

json vec(Vec2 v) { return json::array({v.x, v.y}); }
Vec2 vec(const json& j, Vec2 def) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return def;
    return {j[0].get<float>(), j[1].get<float>()};
}

std::string hex(const Color& c) {
    auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    char buf[16];
    if (b(c.a) == 255) std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", b(c.r), b(c.g), b(c.b));
    else std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", b(c.r), b(c.g), b(c.b), b(c.a));
    return buf;
}
Color color(const json& j, Color def) {
    if (!j.is_string()) return def;
    const std::string s = j.get<std::string>();
    if (s.size() != 7 && s.size() != 9) return def;
    if (s[0] != '#') return def;
    unsigned v = 0;
    if (std::sscanf(s.c_str() + 1, "%x", &v) != 1) return def;
    if (s.size() == 7) return Color::rgb(v);
    return {((v >> 24) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f};
}

template <typename T>
T get(const json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        return def;
    }
}

constexpr const char* kShapeNames[] = {"rectangle", "rounded_rectangle", "circle", "capsule"};

json graphToJson(const NodeGraph& g) {
    json nodes = json::array(), links = json::array();
    for (const Node& n : g.nodes) {
        json values = json::object();
        for (const auto& [k, v] : n.values) values[k] = v;
        nodes.push_back({{"id", n.id}, {"type", n.type}, {"pos", json::array({n.x, n.y})}, {"values", values}});
    }
    for (const Link& l : g.links)
        links.push_back({{"from", json::array({l.fromNode, l.fromPin})}, {"to", json::array({l.toNode, l.toPin})}});
    return {{"nodes", nodes}, {"links", links}};
}

NodeGraph graphFromJson(const json& j) {
    NodeGraph g;
    for (const json& n : j.value("nodes", json::array())) {
        Node node;
        node.id = get(n, "id", 0);
        node.type = get(n, "type", std::string());
        const Vec2 p = vec(n.value("pos", json()), {});
        node.x = p.x;
        node.y = p.y;
        const json values = n.value("values", json::object()); // a temporary would die before the loop
        for (const auto& [k, v] : values.items())
            node.values[k] = v.is_string() ? v.get<std::string>() : v.dump();
        if (node.id > 0 && !node.type.empty() && !g.find(node.id)) g.nodes.push_back(std::move(node));
    }
    for (const json& l : j.value("links", json::array())) {
        const json from = l.value("from", json()), to = l.value("to", json());
        if (!from.is_array() || from.size() != 2 || !to.is_array() || to.size() != 2) continue;
        try {
            g.links.push_back({from[0].get<int>(), from[1].get<std::string>(), to[0].get<int>(), to[1].get<std::string>()});
        } catch (const json::exception&) {
        }
    }
    return g;
}

json entityToJson(const Entity& e) {
    json j = {{"id", e.id}, {"name", e.name}, {"position", vec(e.position)}};
    if (!e.visible) j["visible"] = false;
    if (e.rotation != 0.0f) j["rotation"] = e.rotation;
    if (e.scale != Vec2{1, 1}) j["scale"] = vec(e.scale);
    if (e.layer != 0) j["layer"] = e.layer;
    if (e.shape) {
        const Shape& s = *e.shape;
        j["shape"] = {{"kind", kShapeNames[static_cast<int>(s.kind)]}, {"size", vec(s.size)}, {"color", hex(s.color)},
                      {"corner_radius", s.cornerRadius}, {"outline", s.outline}, {"outline_color", hex(s.outlineColor)},
                      {"glow", s.glow}, {"lit", s.lit}};
    }
    if (e.sprite)
        j["sprite"] = {{"image", e.sprite->image}, {"size", vec(e.sprite->size)}, {"tint", hex(e.sprite->tint)}, {"lit", e.sprite->lit}};
    if (e.light) j["light"] = {{"color", hex(e.light->color)}, {"radius", e.light->radius}, {"intensity", e.light->intensity}};
    if (!e.script.empty()) j["script"] = e.script;
    if (e.graph) j["graph"] = graphToJson(*e.graph);
    return j;
}

Entity entityFromJson(const json& j) {
    Entity e;
    e.id = get(j, "id", 0u);
    e.name = get(j, "name", std::string("Object"));
    e.visible = get(j, "visible", true);
    e.position = vec(j.value("position", json()), {});
    e.rotation = get(j, "rotation", 0.0f);
    e.scale = vec(j.value("scale", json()), {1, 1});
    e.layer = get(j, "layer", 0);
    if (auto it = j.find("shape"); it != j.end() && it->is_object()) {
        Shape s;
        const std::string kind = get(*it, "kind", std::string("rectangle"));
        for (int i = 0; i < 4; ++i)
            if (kind == kShapeNames[i]) s.kind = static_cast<ShapeKind>(i);
        s.size = vec(it->value("size", json()), s.size);
        s.color = color(it->value("color", json()), s.color);
        s.cornerRadius = get(*it, "corner_radius", s.cornerRadius);
        s.outline = get(*it, "outline", s.outline);
        s.outlineColor = color(it->value("outline_color", json()), s.outlineColor);
        s.glow = get(*it, "glow", s.glow);
        s.lit = get(*it, "lit", s.lit);
        e.shape = s;
    }
    if (auto it = j.find("sprite"); it != j.end() && it->is_object()) {
        Sprite s;
        s.image = get(*it, "image", std::string());
        s.size = vec(it->value("size", json()), s.size);
        s.tint = color(it->value("tint", json()), s.tint);
        s.lit = get(*it, "lit", s.lit);
        e.sprite = s;
    }
    if (auto it = j.find("light"); it != j.end() && it->is_object()) {
        Light l;
        l.color = color(it->value("color", json()), l.color);
        l.radius = get(*it, "radius", l.radius);
        l.intensity = get(*it, "intensity", l.intensity);
        e.light = l;
    }
    e.script = get(j, "script", std::string());
    if (auto it = j.find("graph"); it != j.end() && it->is_object()) e.graph = graphFromJson(*it);
    return e;
}

json sceneJson(const Scene& s) {
    json entities = json::array();
    for (const Entity& e : s.entities) entities.push_back(entityToJson(e));
    json j = {{"name", s.name}, {"background", hex(s.background)}, {"ambient", hex(s.ambient)},
              {"camera", {{"position", vec(s.camera.position)}, {"zoom", s.camera.zoom}}}, {"entities", entities}};
    if (!s.script.empty()) j["script"] = s.script;
    if (s.graph) j["graph"] = graphToJson(*s.graph);
    return j;
}

Scene sceneFrom(const json& j) {
    Scene s;
    s.name = get(j, "name", s.name);
    s.background = color(j.value("background", json()), s.background);
    s.ambient = color(j.value("ambient", json()), s.ambient);
    if (auto it = j.find("camera"); it != j.end() && it->is_object()) {
        s.camera.position = vec(it->value("position", json()), {});
        s.camera.zoom = std::max(0.01f, get(*it, "zoom", 1.0f));
    }
    for (const json& e : j.value("entities", json::array()))
        if (e.is_object()) s.entities.push_back(entityFromJson(e));
    s.script = get(j, "script", std::string());
    if (auto it = j.find("graph"); it != j.end() && it->is_object()) s.graph = graphFromJson(*it);
    // Ids are unique and not zero: a hand-edited file is repaired, not refused.
    for (size_t i = 0; i < s.entities.size(); ++i) {
        Entity& e = s.entities[i];
        bool clash = e.id == 0;
        for (size_t k = 0; k < i && !clash; ++k) clash = s.entities[k].id == e.id;
        if (clash) e.id = s.nextId();
    }
    return s;
}

} // namespace

std::string projectToJson(const Project& p) {
    json scenes = json::array();
    for (const Scene& s : p.scenes) scenes.push_back(sceneJson(s));
    json j = {{"artisan", kFormatVersion},
              {"name", p.name},
              {"window",
               {{"width", p.window.width}, {"height", p.window.height}, {"fullscreen", p.window.fullscreen},
                {"vsync", p.window.vsync}, {"pixel_art", p.window.pixelArt}}},
              {"main_scene", p.mainScene},
              {"scenes", scenes}};
    return j.dump(2) + "\n";
}

bool projectFromJson(const std::string& text, Project& out, std::string* error) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (error) *error = "Not a valid project file (JSON)";
        return false;
    }
    if (get(j, "artisan", 0) > kFormatVersion) {
        if (error) *error = "Made by a newer Artisan: update to open it";
        return false;
    }
    Project p;
    p.name = get(j, "name", p.name);
    if (auto it = j.find("window"); it != j.end() && it->is_object()) {
        p.window.width = std::clamp(get(*it, "width", 1280), 16, 16384);
        p.window.height = std::clamp(get(*it, "height", 720), 16, 16384);
        p.window.fullscreen = get(*it, "fullscreen", false);
        p.window.vsync = get(*it, "vsync", true);
        p.window.pixelArt = get(*it, "pixel_art", false);
    }
    p.scenes.clear();
    for (const json& s : j.value("scenes", json::array()))
        if (s.is_object()) p.scenes.push_back(sceneFrom(s));
    if (p.scenes.empty()) p.scenes.emplace_back();
    p.mainScene = std::clamp(get(j, "main_scene", 0), 0, static_cast<int>(p.scenes.size()) - 1);
    out = std::move(p);
    return true;
}

std::string sceneToJson(const Scene& scene) { return sceneJson(scene).dump(); }

bool sceneFromJson(const std::string& text, Scene& out) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    out = sceneFrom(j);
    return true;
}

const char* const kStarterPlayerScript = R"(-- The player: arrow keys or WASD move it, the camera follows.
-- start(self) runs once, update(self, dt) every frame. self.x, self.y, self.rotation,
-- self.scale_x, self.scale_y and self.visible are the object's; see docs/SCRIPTING.md.

local speed = 320

function start(self)
    log("Hello from " .. self.name)
end

function update(self, dt)
    local dx = (input.down("right") or input.down("d")) and 1 or 0
    dx = dx - ((input.down("left") or input.down("a")) and 1 or 0)
    local dy = (input.down("down") or input.down("s")) and 1 or 0
    dy = dy - ((input.down("up") or input.down("w")) and 1 or 0)
    self.x = self.x + dx * speed * dt
    self.y = self.y + dy * speed * dt
    if input.pressed("space") then
        self:set_color(math.random(), math.random(), math.random(), 1)
    end
    camera.x = camera.x + (self.x - camera.x) * math.min(1, 6 * dt)
    camera.y = camera.y + (self.y - camera.y) * math.min(1, 6 * dt)
end
)";

Project starterProject(const std::string& name, int width, int height) {
    Project p;
    p.name = name;
    p.window.width = width;
    p.window.height = height;
    Scene& s = p.scene();
    s.camera.position = {0, 0};

    Entity floor;
    floor.name = "Floor";
    floor.layer = -1;
    floor.shape = Shape{ShapeKind::RoundedRectangle, {1400, 900}, Color::rgb(0x23232B), 28.0f, 2.0f, Color::rgb(0x34343F), 0.0f, true};
    s.add(floor);

    const Color pillars[] = {Color::rgb(0x4FB3FF), Color::rgb(0xFF5FA2), Color::rgb(0x7CE38B)};
    for (int i = 0; i < 3; ++i) {
        Entity e;
        e.name = "Pillar";
        e.position = {-420.0f + i * 420.0f, -240.0f};
        e.shape = Shape{ShapeKind::Circle, {70, 70}, pillars[i], 0.0f, 0.0f, {}, 26.0f, true};
        s.add(e);
    }

    Entity spinner;
    spinner.name = "Spinner";
    spinner.position = {0, 220};
    spinner.shape = Shape{ShapeKind::RoundedRectangle, {120, 120}, Color::rgb(0xB48CFF), 22.0f, 3.0f, Color::rgb(0xFFFFFF, 0.6f), 18.0f, true};
    NodeGraph g;
    Node& ev = g.add("event.update", 40, 60);
    Node& rot = g.add("action.rotate", 300, 60);
    Node& mul = g.add("math.multiply", 60, 200);
    mul.values["b"] = "90";
    g.connect({ev.id, "then", rot.id, "in"});
    g.connect({ev.id, "dt", mul.id, "a"});
    g.connect({mul.id, "result", rot.id, "degrees"});
    spinner.graph = g;
    s.add(spinner);

    Entity player;
    player.name = "Player";
    player.layer = 1;
    player.shape = Shape{ShapeKind::Capsule, {56, 56}, Color::rgb(0xE8B931), 0.0f, 0.0f, {}, 14.0f, false};
    player.light = Light{Color::rgb(0xFFD58A), 380.0f, 1.3f};
    player.script = "scripts/player.lua";
    s.add(player);

    Entity lamp;
    lamp.name = "Lamp";
    lamp.position = {-480, 260};
    lamp.light = Light{Color::rgb(0x5FA8FF), 460.0f, 1.0f};
    s.add(lamp);
    return p;
}

} // namespace artisan
