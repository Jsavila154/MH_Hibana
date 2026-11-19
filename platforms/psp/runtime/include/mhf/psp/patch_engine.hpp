#pragma once

#include "mods/data_mod_parser.hpp"
#include "mods/mod.hpp"
#include "psprecomp/guest_memory.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mhf::psp {

// Applies, verifies, and cleanly reverts memory patches in GuestMemory, including reapplication when overlays reload.
class PatchEngine {
public:
    explicit PatchEngine(psprecomp::GuestMemory &memory) : memory_(memory) {}

    struct Entry {
        core::mods::PatchSpec spec;
        bool applied{false};
        std::vector<std::uint8_t> original;
    };

    bool apply_group(std::string_view group_id, const std::vector<core::mods::PatchSpec> &specs);
    bool revert_group(std::string_view group_id);
    void on_overlay_loaded(std::string_view overlay_name);
    [[nodiscard]] bool is_group_applied(std::string_view group_id) const noexcept;

private:
    bool apply_entry(Entry &e);
    void revert_entry(Entry &e);

    psprecomp::GuestMemory &memory_;
    std::unordered_map<std::string, std::vector<Entry>> groups_;
};

// File-backed data mod driven by a parsed mod.toml manifest and executed via PatchEngine.
class DataMod final : public core::mods::IMod {
public:
    DataMod(core::mods::DataModManifest manifest, PatchEngine &engine)
        : manifest_(std::move(manifest)), engine_(engine) {}

    [[nodiscard]] const core::mods::ModInfo &info() const noexcept override { return manifest_.info; }
    [[nodiscard]] bool is_enabled() const noexcept override { return enabled_; }
    bool set_enabled(bool enabled) override;
    void on_overlay_loaded(std::string_view overlay_name) override;

private:
    core::mods::DataModManifest manifest_;
    PatchEngine &engine_;
    bool enabled_{false};
};

} // namespace mhf::psp
