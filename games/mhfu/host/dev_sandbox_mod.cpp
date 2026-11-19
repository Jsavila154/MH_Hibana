#include "dev_sandbox_mod.hpp"

#include "mhfu_region_data.hpp"

#include <string_view>

namespace mhf::mhfu {
namespace {

namespace region = mhf::psp::generated::mhfu_region_data;

constexpr bool kDevSandboxSupported = region::mh_game_sys.has_value();

DevSandboxMod *g_dev_sandbox{nullptr};

bool is_village_overlay(const psp::Runtime &rt, std::uint32_t addr) {
    const mhf::psp::Program *overlay = rt.active_overlay_at(addr);
    return overlay != nullptr && std::string_view(overlay->name).ends_with("_ovl_lobby_task");
}

void hook_forge_unlock(psp::Runtime &rt, psp::Context &ctx) {
    if (g_dev_sandbox) g_dev_sandbox->on_forge_unlock(rt, ctx);
}
void hook_zenny_check(psp::Runtime &rt, psp::Context &ctx) {
    if (g_dev_sandbox) g_dev_sandbox->on_zenny_check(rt, ctx);
}
void hook_free_craft(psp::Runtime &rt, psp::Context &ctx) {
    if (g_dev_sandbox) g_dev_sandbox->on_free_craft(rt, ctx);
}

} // namespace

DevSandboxMod::DevSandboxMod(psp::Runtime &runtime)
    : runtime_(runtime),
      info_{"dev_sandbox",
            "Dev Mode: Unlock & Free Craft",
            "Unlocks all Smithy equipment and allows crafting without spending materials or running out of zenny.",
            core::mods::ModCategory::Gameplay,
            false,
            false,
            kDevSandboxSupported ? "" : "Unavailable for this disc region."} {
    g_dev_sandbox = this;
    if constexpr (kDevSandboxSupported) {
        runtime_.enable_patch("mh_mod_dev_forge_unlock");
        runtime_.enable_patch("mh_mod_dev_zenny_check");
        runtime_.enable_patch("mh_mod_dev_free_craft");
        runtime_.register_hook("mh_mod_dev_forge_unlock", &hook_forge_unlock);
        runtime_.register_hook("mh_mod_dev_zenny_check", &hook_zenny_check);
        runtime_.register_hook("mh_mod_dev_free_craft", &hook_free_craft);
    }
}

DevSandboxMod::~DevSandboxMod() {
    if (g_dev_sandbox == this) g_dev_sandbox = nullptr;
}

bool DevSandboxMod::set_enabled(bool enabled) {
    if (!kDevSandboxSupported) {
        enabled_ = false;
        return false;
    }
    enabled_ = enabled;
    return true;
}

void DevSandboxMod::on_forge_unlock(psp::Runtime &, psp::Context &ctx) {
    if (!enabled_.load()) return;
    ctx.gpr[3] = 15u; // pretend all 4 material slots are satisfied -> returns 2 (unlocked + craftable)
}

void DevSandboxMod::on_zenny_check(psp::Runtime &rt, psp::Context &ctx) {
    if (!enabled_.load() || !is_village_overlay(rt, 0x09A800F4u)) return;
    auto &mem = rt.memory();
    const std::uint32_t game_sys = mem.load32(*region::mh_game_sys);
    if (game_sys != 0u) {
        const std::uint32_t zenny_addr = game_sys + (7u << 16u) - 26896u;
        if (mem.contains(zenny_addr, 4u) && mem.load32(zenny_addr) < 1'000'000u) {
            mem.store32(zenny_addr, 1'000'000u);
        }
    }
    ctx.gpr[5] = 0u;
}

void DevSandboxMod::on_free_craft(psp::Runtime &rt, psp::Context &ctx) {
    if (!enabled_.load() || !is_village_overlay(rt, 0x09AA5C98u)) return;
    ctx.gpr[3] = 0u; // skip material deduction
}

} // namespace mhf::mhfu
