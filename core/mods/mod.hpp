#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mhf::core::mods {

enum class ModCategory {
    Gameplay,
    Cosmetic,
    QualityOfLife,
};

struct ModInfo {
    std::string id;
    std::string name;
    std::string description;
    ModCategory category{ModCategory::Gameplay};
    bool requires_zone_reload{false};
    bool online_safe{false};
    std::string status_note;
};

// Base interface implemented by both built-in C++ mods and file-based data mods.
class IMod {
public:
    virtual ~IMod() = default;

    [[nodiscard]] virtual const ModInfo &info() const noexcept = 0;
    [[nodiscard]] virtual bool is_enabled() const noexcept = 0;
    virtual bool set_enabled(bool enabled) = 0;
    virtual void on_overlay_loaded(std::string_view overlay_name) { (void)overlay_name; }
    virtual void on_tick() {}
};

// Owns registered mods, tracks online safety state, and persists enabled flags.
class ModRegistry {
public:
    void register_mod(std::unique_ptr<IMod> mod);
    [[nodiscard]] IMod *find(std::string_view id) noexcept;
    [[nodiscard]] const IMod *find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<IMod>> &mods() const noexcept { return mods_; }

    bool set_enabled(std::string_view id, bool enabled);
    void set_online_active(bool online);
    [[nodiscard]] bool online_active() const noexcept { return online_active_; }

    void on_overlay_loaded(std::string_view overlay_name);
    void on_tick();

    bool save(const std::filesystem::path &path) const;
    bool load(const std::filesystem::path &path);

private:
    std::vector<std::unique_ptr<IMod>> mods_;
    std::vector<std::string> suspended_for_online_;
    bool online_active_{false};
};

} // namespace mhf::core::mods
