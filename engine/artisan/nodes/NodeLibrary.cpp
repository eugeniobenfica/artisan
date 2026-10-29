#include "artisan/nodes/NodeLibrary.h"

namespace artisan {

namespace {

using Kind = NodeDef::Kind;
using Ctx = EmitContext;

PinDef execIn() { return {"in", PinType::Exec, ""}; }
PinDef execOut(std::string name = "then") { return {std::move(name), PinType::Exec, ""}; }
PinDef num(std::string name, std::string def = "0") { return {std::move(name), PinType::Number, std::move(def)}; }
PinDef boolean(std::string name, std::string def = "false") { return {std::move(name), PinType::Bool, std::move(def)}; }
PinDef text(std::string name, std::string def = "") { return {std::move(name), PinType::String, std::move(def)}; }
PinDef any(std::string name, std::string def = "") { return {std::move(name), PinType::Any, std::move(def)}; }

NodeDef action(std::string type, std::string title, std::vector<PinDef> inputs,
               std::function<std::string(const Ctx&)> body) {
    NodeDef d;
    d.type = std::move(type);
    d.title = std::move(title);
    d.category = "Actions";
    d.kind = Kind::Action;
    d.inputs.push_back(execIn());
    for (auto& p : inputs) d.inputs.push_back(std::move(p));
    d.outputs = {execOut()};
    d.statements = [body = std::move(body)](const Ctx& c) { return body(c) + c.exec("then"); };
    return d;
}

NodeDef pure(std::string type, std::string title, std::string category, std::vector<PinDef> inputs,
             std::vector<PinDef> outputs, std::function<std::string(const Ctx&, const std::string&)> expr) {
    NodeDef d;
    d.type = std::move(type);
    d.title = std::move(title);
    d.category = std::move(category);
    d.kind = Kind::Pure;
    d.inputs = std::move(inputs);
    d.outputs = std::move(outputs);
    d.expression = std::move(expr);
    return d;
}

NodeDef binary(std::string type, std::string title, std::string category, const char* op, PinType a, PinType out) {
    std::vector<PinDef> ins = a == PinType::Bool ? std::vector<PinDef>{boolean("a"), boolean("b")} : std::vector<PinDef>{num("a"), num("b")};
    const PinDef result{"result", out, ""};
    return pure(std::move(type), std::move(title), std::move(category), ins, {result},
                [op = std::string(op)](const Ctx& c, const std::string&) { return "(" + c.input("a") + " " + op + " " + c.input("b") + ")"; });
}

std::vector<NodeDef> build() {
    std::vector<NodeDef> lib;

    // --- events: where a chain starts ------------------------------------------------
    {
        NodeDef d;
        d.type = "event.start";
        d.title = "On Start";
        d.category = "Events";
        d.kind = Kind::Event;
        d.event = "start";
        d.outputs = {execOut()};
        d.statements = [](const Ctx& c) { return c.exec("then"); };
        lib.push_back(d);

        d.type = "event.update";
        d.title = "On Update";
        d.event = "update";
        d.outputs = {execOut(), num("dt")};
        d.expression = [](const Ctx&, const std::string&) { return std::string("dt"); };
        lib.push_back(d);

        d.type = "event.key_pressed";
        d.title = "On Key Pressed";
        d.inputs = {text("key", "space")};
        d.outputs = {execOut()};
        d.expression = nullptr;
        d.statements = [](const Ctx& c) { return "if input.pressed(" + c.input("key") + ") then\n" + c.exec("then") + "end\n"; };
        lib.push_back(d);

        d.type = "event.key_released";
        d.title = "On Key Released";
        d.statements = [](const Ctx& c) { return "if input.released(" + c.input("key") + ") then\n" + c.exec("then") + "end\n"; };
        lib.push_back(d);

        d.type = "event.key_down";
        d.title = "While Key Down";
        d.statements = [](const Ctx& c) { return "if input.down(" + c.input("key") + ") then\n" + c.exec("then") + "end\n"; };
        lib.push_back(d);
    }

    // --- flow ----------------------------------------------------------------------------
    {
        NodeDef d;
        d.type = "flow.branch";
        d.title = "Branch";
        d.category = "Flow";
        d.inputs = {execIn(), boolean("condition")};
        d.outputs = {execOut("true"), execOut("false")};
        d.statements = [](const Ctx& c) {
            const std::string f = c.exec("false");
            return "if " + c.input("condition") + " then\n" + c.exec("true") + (f.empty() ? "" : "else\n" + f) + "end\n";
        };
        lib.push_back(d);

        d.type = "flow.sequence";
        d.title = "Sequence";
        d.inputs = {execIn()};
        d.outputs = {execOut("first"), execOut("second"), execOut("third")};
        d.statements = [](const Ctx& c) { return c.exec("first") + c.exec("second") + c.exec("third"); };
        lib.push_back(d);
    }

    // --- actions on the object -----------------------------------------------------------
    lib.push_back(action("action.move", "Move By", {num("dx"), num("dy")}, [](const Ctx& c) {
        return "self.x = self.x + " + c.input("dx") + "\nself.y = self.y + " + c.input("dy") + "\n";
    }));
    lib.push_back(action("action.set_position", "Set Position", {num("x"), num("y")},
                         [](const Ctx& c) { return "self.x = " + c.input("x") + "\nself.y = " + c.input("y") + "\n"; }));
    lib.push_back(action("action.rotate", "Rotate By", {num("degrees", "90")},
                         [](const Ctx& c) { return "self.rotation = self.rotation + " + c.input("degrees") + "\n"; }));
    lib.push_back(action("action.set_rotation", "Set Rotation", {num("degrees")},
                         [](const Ctx& c) { return "self.rotation = " + c.input("degrees") + "\n"; }));
    lib.push_back(action("action.set_scale", "Set Scale", {num("x", "1"), num("y", "1")},
                         [](const Ctx& c) { return "self.scale_x = " + c.input("x") + "\nself.scale_y = " + c.input("y") + "\n"; }));
    lib.push_back(action("action.set_visible", "Set Visible", {boolean("visible", "true")},
                         [](const Ctx& c) { return "self.visible = " + c.input("visible") + "\n"; }));
    lib.push_back(action("action.set_color", "Set Color", {num("r", "1"), num("g", "1"), num("b", "1"), num("a", "1")}, [](const Ctx& c) {
        return "self:set_color(" + c.input("r") + ", " + c.input("g") + ", " + c.input("b") + ", " + c.input("a") + ")\n";
    }));
    lib.push_back(action("action.camera_follow", "Camera Follow", {num("smoothing", "8")}, [](const Ctx& c) {
        const std::string k = "math.min(1, " + c.input("smoothing") + " * time.dt)";
        return "camera.x = camera.x + (self.x - camera.x) * " + k + "\ncamera.y = camera.y + (self.y - camera.y) * " + k + "\n";
    }));
    lib.push_back(action("action.log", "Print", {any("value", "Hello")}, [](const Ctx& c) { return "log(" + c.input("value") + ")\n"; }));
    lib.push_back(action("action.set_var", "Set Variable", {text("name", "score"), any("value", "0")},
                         [](const Ctx& c) { return "vars[" + c.input("name") + "] = " + c.input("value") + "\n"; }));
    lib.back().category = "Variables";

    // --- collision: hitboxes -------------------------------------------------------------------
    {
        NodeDef d;
        d.type = "event.hit";
        d.title = "On Hit";
        d.category = "Collision";
        d.kind = Kind::Event;
        d.event = "hit";
        d.inputs = {text("with")}; // only that object (by name); empty: any
        d.outputs = {execOut(), text("other")};
        d.statements = [](const Ctx& c) {
            return "if " + c.input("with") + " == \"\" or other.name == " + c.input("with") + " then\n" + c.exec("then") + "end\n";
        };
        d.expression = [](const Ctx&, const std::string&) { return std::string("other.name"); };
        lib.push_back(d);
    }
    lib.push_back(pure("hitbox.touching", "Is Touching", "Collision", {text("name")}, {boolean("touching")},
                       [](const Ctx& c, const std::string&) { return "self:touching(" + c.input("name") + ")"; }));

    // --- script: Lua typed into the graph ------------------------------------------------------
    {
        // Its code runs as a function of `a` and `b` (with `self`, `dt` and the whole API in
        // reach); what it returns comes out of `result`. It is code the user wrote, on purpose:
        // the one node whose text is not a literal.
        NodeDef d = action("script.lua", "Lua Code", {any("a"), any("b")}, [](const Ctx& c) {
            std::string code = c.value("code");
            if (!code.empty() && code.back() != '\n') code += "\n";
            return "local " + c.uid + "_result = (function(a, b)\n" + code + "end)(" + c.input("a") + ", " + c.input("b") + ")\n";
        });
        d.category = "Script";
        d.hasCode = true;
        d.outputs.push_back(any("result"));
        d.expression = [](const Ctx& c, const std::string&) { return c.uid + "_result"; };
        lib.push_back(d);
    }

    // --- values ------------------------------------------------------------------------------
    const PinDef result{"result", PinType::Number, ""};
    lib.push_back(pure("value.number", "Number", "Values", {num("value")}, {num("value")},
                       [](const Ctx& c, const std::string&) { return c.input("value"); }));
    lib.push_back(pure("value.text", "Text", "Values", {text("value")}, {text("value")},
                       [](const Ctx& c, const std::string&) { return c.input("value"); }));
    lib.push_back(pure("var.get", "Get Variable", "Variables", {text("name", "score")}, {any("value")},
                       [](const Ctx& c, const std::string&) { return "(vars[" + c.input("name") + "] or 0)"; }));
    lib.push_back(pure("self.position", "Get Position", "Object", {}, {num("x"), num("y")},
                       [](const Ctx&, const std::string& pin) { return "self." + pin; }));
    lib.push_back(pure("self.rotation", "Get Rotation", "Object", {}, {num("degrees")},
                       [](const Ctx&, const std::string&) { return std::string("self.rotation"); }));
    lib.push_back(pure("time.delta", "Delta Time", "Time", {}, {num("dt")},
                       [](const Ctx&, const std::string&) { return std::string("time.dt"); }));
    lib.push_back(pure("time.now", "Time", "Time", {}, {num("seconds")},
                       [](const Ctx&, const std::string&) { return std::string("time.now"); }));
    lib.push_back(pure("input.key_down", "Is Key Down", "Input", {text("key", "right")}, {boolean("down")},
                       [](const Ctx& c, const std::string&) { return "input.down(" + c.input("key") + ")"; }));
    lib.push_back(pure("input.axis", "Input Axis", "Input", {text("negative", "left"), text("positive", "right")}, {num("value")},
                       [](const Ctx& c, const std::string&) {
                           return "((input.down(" + c.input("positive") + ") and 1 or 0) - (input.down(" + c.input("negative") + ") and 1 or 0))";
                       }));

    // --- math and logic ------------------------------------------------------------------------
    lib.push_back(binary("math.add", "Add", "Math", "+", PinType::Number, PinType::Number));
    lib.push_back(binary("math.subtract", "Subtract", "Math", "-", PinType::Number, PinType::Number));
    lib.push_back(binary("math.multiply", "Multiply", "Math", "*", PinType::Number, PinType::Number));
    lib.push_back(binary("math.divide", "Divide", "Math", "/", PinType::Number, PinType::Number));
    lib.push_back(pure("math.sin", "Sine", "Math", {num("value")}, {result},
                       [](const Ctx& c, const std::string&) { return "math.sin(" + c.input("value") + ")"; }));
    lib.push_back(pure("math.random", "Random", "Math", {num("min"), num("max", "1")}, {result}, [](const Ctx& c, const std::string&) {
        return "(" + c.input("min") + " + math.random() * (" + c.input("max") + " - " + c.input("min") + "))";
    }));
    lib.push_back(binary("math.greater", "Greater", "Logic", ">", PinType::Number, PinType::Bool));
    lib.push_back(binary("math.less", "Less", "Logic", "<", PinType::Number, PinType::Bool));
    lib.push_back(binary("math.equal", "Equal", "Logic", "==", PinType::Number, PinType::Bool));
    lib.push_back(binary("logic.and", "And", "Logic", "and", PinType::Bool, PinType::Bool));
    lib.push_back(binary("logic.or", "Or", "Logic", "or", PinType::Bool, PinType::Bool));
    lib.push_back(pure("logic.not", "Not", "Logic", {boolean("value")}, {boolean("result")},
                       [](const Ctx& c, const std::string&) { return "(not " + c.input("value") + ")"; }));
    return lib;
}

} // namespace

const PinDef* NodeDef::input(const std::string& name) const {
    for (const PinDef& p : inputs)
        if (p.name == name) return &p;
    return nullptr;
}

const PinDef* NodeDef::output(const std::string& name) const {
    for (const PinDef& p : outputs)
        if (p.name == name) return &p;
    return nullptr;
}

const std::vector<NodeDef>& nodeLibrary() {
    static const std::vector<NodeDef> lib = build();
    return lib;
}

const NodeDef* findNodeDef(const std::string& type) {
    for (const NodeDef& d : nodeLibrary())
        if (d.type == type) return &d;
    return nullptr;
}

} // namespace artisan
