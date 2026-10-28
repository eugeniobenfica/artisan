#include "app/ProjectIO.h"

#include "app/Platform.h"

#include <algorithm>
#include <chrono>
#include <string_view>

namespace artisan::editor {

std::string safeFileName(std::string name) {
    for (char& ch : name)
        if (std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos || static_cast<unsigned char>(ch) < 32) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    return name.empty() ? "Untitled" : name;
}

bool loadProject(const fs::path& dir, Project& out, std::string* error) {
    const auto text = platform::readFile(dir / kProjectFile);
    if (!text) {
        if (error) *error = "No project in " + platform::utf8(dir);
        return false;
    }
    return projectFromJson(*text, out, error);
}

bool saveProject(const fs::path& dir, const Project& project, std::string* error) {
    return platform::writeFile(dir / kProjectFile, projectToJson(project), error);
}

std::string newScriptTemplate(const std::string& objectName) {
    return "-- " + objectName +
           "\n-- start(self) runs once when the game starts; update(self, dt) every frame (dt: seconds).\n"
           "-- self.x, self.y, self.rotation, self.scale_x, self.scale_y, self.visible; input.down(\"left\"),\n"
           "-- input.pressed(\"space\"); camera.x, camera.y, camera.zoom; log(...). See docs/SCRIPTING.md.\n\n"
           "function start(self)\nend\n\nfunction update(self, dt)\nend\n";
}

fs::path createProject(const fs::path& parent, const std::string& name, ProjectTemplate kind, int width, int height,
                       std::string* error) {
    std::error_code ec;
    const std::string base = safeFileName(name);
    fs::path dir = parent / platform::fromUtf8(base);
    for (int i = 2; fs::exists(dir, ec); ++i) dir = parent / platform::fromUtf8(base + " " + std::to_string(i));
    if (!fs::create_directories(dir / "scripts", ec) || !fs::create_directories(dir / "assets", ec)) {
        if (error) *error = "Cannot create " + platform::utf8(dir);
        return {};
    }
    Project p;
    if (kind == ProjectTemplate::Starter) {
        p = starterProject(name, width, height);
        if (!platform::writeFile(dir / "scripts" / "player.lua", kStarterPlayerScript, error)) return {};
    } else {
        p.name = name;
        p.window.width = width;
        p.window.height = height;
    }
    if (!saveProject(dir, p, error)) return {};
    return dir;
}

std::vector<ProjectEntry> findProjects(const fs::path& folder, const std::vector<std::string>& extra) {
    std::vector<ProjectEntry> out;
    auto add = [&](const fs::path& dir) {
        std::error_code ec;
        const fs::path file = dir / kProjectFile;
        if (!fs::is_regular_file(file, ec)) return;
        for (const ProjectEntry& e : out)
            if (fs::equivalent(e.dir, dir, ec)) return;
        ProjectEntry e;
        e.dir = dir;
        e.name = platform::utf8(dir.filename());
        const auto t = fs::last_write_time(file, ec);
        if (!ec) {
            using namespace std::chrono;
            // File clock -> system clock through "now" (std::clock_cast is missing from libc++).
            const auto sys = system_clock::now() + duration_cast<system_clock::duration>(t - fs::file_time_type::clock::now());
            e.modified = duration_cast<seconds>(sys.time_since_epoch()).count();
        }
        out.push_back(std::move(e));
    };
    for (const std::string& r : extra) add(platform::fromUtf8(r));
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(folder, ec)) {
        int scanned = 0;
        for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator() && scanned < 2000; it.increment(ec), ++scanned) {
            if (it.depth() >= 2) it.disable_recursion_pending();
            std::error_code fe;
            if (it->is_directory(fe) && fs::is_regular_file(it->path() / kProjectFile, fe)) {
                add(it->path());
                it.disable_recursion_pending(); // a project's own folders are not more projects
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ProjectEntry& a, const ProjectEntry& b) { return a.modified > b.modified; });
    return out;
}

} // namespace artisan::editor
