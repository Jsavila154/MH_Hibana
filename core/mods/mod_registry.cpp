#include "mods/mod.hpp"

#include <algorithm>
#include <fstream>

namespace mhf::core::mods {

void ModRegistry::register_mod(std::unique_ptr<IMod> mod) {
    if (!mod) return;
    if (find(mod->info().id) != nullptr) return;
    mods_.push_back(std::move(mod));
}

IMod *ModRegistry::find(std::string_view id) noexcept {
    for (auto &m : mods_) {
        if (m->info().id == id) return m.get();
    }
    return nullptr;
}

const IMod *ModRegistry::find(std::string_view id) const noexcept {
    for (const auto &m : mods_) {
        if (m->info().id == id) return m.get();
    }
    return nullptr;
}

bool ModRegistry::set_enabled(std::string_view id, bool enabled) {
    IMod *mod = find(id);
    if (mod == nullptr) return false;
    if (online_active_ && enabled && !mod->info().online_safe) return false;
    return mod->set_enabled(enabled);
}

void ModRegistry::set_online_active(bool online) {
    if (online == online_active_) return;
    online_active_ = online;
    if (online) {
        suspended_for_online_.clear();
        for (auto &m : mods_) {
            if (m->is_enabled() && !m->info().online_safe) {
                suspended_for_online_.push_back(m->info().id);
                m->set_enabled(false);
            }
        }
    } else {
        for (const std::string &id : suspended_for_online_) {
            if (IMod *m = find(id)) m->set_enabled(true);
        }
        suspended_for_online_.clear();
    }
}

void ModRegistry::on_overlay_loaded(std::string_view overlay_name) {
    for (auto &m : mods_) {
        if (m->is_enabled()) m->on_overlay_loaded(overlay_name);
    }
}

void ModRegistry::on_tick() {
    for (auto &m : mods_) {
        if (m->is_enabled()) m->on_tick();
    }
}

bool ModRegistry::save(const std::filesystem::path &path) const {
    if (path.empty()) return false;
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const auto &m : mods_) {
        const bool was_suspended =
            std::find(suspended_for_online_.begin(), suspended_for_online_.end(), m->info().id) != suspended_for_online_.end();
        out << m->info().id << '=' << ((m->is_enabled() || was_suspended) ? '1' : '0') << '\n';
    }
    return static_cast<bool>(out);
}

bool ModRegistry::load(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string id = line.substr(0, eq);
        const bool want = (eq + 1 < line.size()) && (line[eq + 1] == '1' || line[eq + 1] == 't');
        set_enabled(id, want);
    }
    return true;
}

} // namespace mhf::core::mods
