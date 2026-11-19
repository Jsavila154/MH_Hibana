#include "piercings_mod.hpp"

#include "mhf/psp/runtime.hpp"
#include "mhfu_region_data.hpp"

#include <array>
#include <cstring>
#include <string_view>

namespace mhf::mhfu {
namespace {

namespace region = mhf::psp::generated::mhfu_region_data;

constexpr std::uint32_t kGuestRecipeBuffer = 0x08802000u;  // unused low guest RAM below text_base (0x08804000)
constexpr std::uint32_t kBaseRecipeIndex = 2156u;          // immediately after the 2156 vanilla armor forge entries
constexpr std::uint32_t kRecipeStride = 26u;
constexpr std::uint32_t kHeadArmorPriceTable = 0x0896325Cu; // u32 price*2 at +0x04 of each 40-byte Head armor entry (ULES01213)
constexpr std::uint32_t kHeadArmorStride = 40u;
constexpr std::uint32_t kPiercingRawPrice = 60000u;         // >> 1 == 30,000z craft cost

struct MaterialSlot {
    std::uint16_t item_id;
    std::uint16_t qty;
};

struct PiercingRecipe {
    std::uint16_t equip_id;
    std::array<MaterialSlot, 4> mats;
};

// 10 Training School Piercings (Sword Saint 309, Barrage 310, and Ambitious 433 remain vanilla quest rewards).
// Tiered by rank: Low Rank (rare drops), High Rank (gems/rubies), G-Rank (Heavenly Scales). All cost 30,000z.
constexpr std::array<PiercingRecipe, 10> kPiercingRecipes{{
    // Low Rank (rare materials from tough LR monsters)
    {245, {{{608, 1}, {551, 1}, {215, 3}, {0, 0}}}}, // Piercing of Pursuit (Tigrex Skull Shl, Rathalos Plate, Lightcrystal)
    {248, {{{523, 1}, {675, 1}, {215, 3}, {0, 0}}}}, // Piercing of Fate    (Rathian Plate, Monoblos Heart, Lightcrystal)
    {249, {{{546, 2}, {608, 1}, {682, 2}, {0, 0}}}}, // Echo Piercing       (FireWyvrn Marrow, Tigrex Skull Shl, Kirin Horn)
    // High Rank (gems/rubies, no final-boss Akantor)
    {241, {{{552, 1}, {300, 2}, {603, 2}, {0, 0}}}}, // Piercing of Rage    (Rathalos Ruby, Wyvern Stone, Tigrex Claw+)
    {243, {{{524, 1}, {211, 2}, {628, 1}, {0, 0}}}}, // Blast Piercing      (Rathian Ruby, Firecell Stone, Gravios BrainStm)
    {244, {{{622, 1}, {300, 2}, {614, 2}, {0, 0}}}}, // Piercing of Mastery (Narga Brain Stem, Wyvern Stone, Nargacuga Tail)
    // G-Rank (Heavenly Scales, no final-boss Ukanlos/Fatalis)
    {240, {{{525, 1}, {583, 2}, {301, 1}, {0, 0}}}}, // Silence Piercing    (HvnlyRathianScl, KhezuSpecialCut, Big Wyvern Stone)
    {242, {{{611, 1}, {596, 1}, {301, 1}, {0, 0}}}}, // Demon Edge Piercing (HvnlyNargaScale, HvnlyTigrexScale, Big Wyvern Stone)
    {246, {{{611, 1}, {690, 2}, {685, 2}, {0, 0}}}}, // Kirin Piercing      (HvnlyNargaScale, Kirin Azure Horn, SpecialKirinHide)
    {247, {{{553, 1}, {596, 1}, {301, 1}, {0, 0}}}}, // Piercing of Might   (HvnlyRathalosScl, HvnlyTigrexScale, Big Wyvern Stone)
}};

constexpr bool kPiercingsSupported =
    region::mh_game_sys.has_value() && region::mh_armor_price.has_value() &&
    region::mh_armor_owned_check.has_value() && region::mh_forge_materials_check.has_value();

bool is_village_overlay(const psp::Runtime &rt, std::uint32_t addr) {
    const mhf::psp::Program *overlay = rt.active_overlay_at(addr);
    return overlay != nullptr && std::string_view(overlay->name).ends_with("_ovl_lobby_task");
}

CraftablePiercingsMod *g_piercings_mod{nullptr};

void hook_unlock_ptr(psp::Runtime &rt, psp::Context &ctx) {
    if (g_piercings_mod) g_piercings_mod->on_forge_recipe_unlock_ptr(rt, ctx);
}
void hook_list_end(psp::Runtime &rt, psp::Context &ctx) {
    if (g_piercings_mod) g_piercings_mod->on_smithy_create_list_populate_end(rt, ctx);
}
void hook_panel_ptr(psp::Runtime &rt, psp::Context &ctx) {
    if (g_piercings_mod) g_piercings_mod->on_smithy_panel_recipe_ptr(rt, ctx);
}
void hook_consume_ptr(psp::Runtime &rt, psp::Context &ctx) {
    if (g_piercings_mod) g_piercings_mod->on_smithy_craft_consume_ptr(rt, ctx);
}

} // namespace

CraftablePiercingsMod::CraftablePiercingsMod(psp::Runtime &runtime)
    : runtime_(runtime),
      info_{"craftable_piercings",
            "Craftable Training Piercings",
            "Makes the 10 Training School Piercings craftable at the Smithy (30,000z, tiered LR/HR/G rare materials).",
            core::mods::ModCategory::Gameplay,
            false,
            true,
            kPiercingsSupported ? "" : "Unavailable for this disc region."} {
    g_piercings_mod = this;
    if constexpr (kPiercingsSupported) {
        for (const char *p : {"mh_mod_piercings_unlock_ptr", "mh_mod_piercings_list_end",
                              "mh_mod_piercings_panel_ptr", "mh_mod_piercings_consume_ptr"}) {
            runtime_.enable_patch(p);
        }
        runtime_.register_hook("mh_mod_piercings_unlock_ptr", &hook_unlock_ptr);
        runtime_.register_hook("mh_mod_piercings_list_end", &hook_list_end);
        runtime_.register_hook("mh_mod_piercings_panel_ptr", &hook_panel_ptr);
        runtime_.register_hook("mh_mod_piercings_consume_ptr", &hook_consume_ptr);
    }
}

CraftablePiercingsMod::~CraftablePiercingsMod() {
    if (g_piercings_mod == this) g_piercings_mod = nullptr;
}

bool CraftablePiercingsMod::set_enabled(bool enabled) {
    if (!kPiercingsSupported) {
        enabled_ = false;
        return false;
    }
    if (enabled == enabled_.load()) return true;
    enabled_ = enabled;
    if (enabled) ensure_guest_table();
    apply_prices(enabled);
    return true;
}

void CraftablePiercingsMod::apply_prices(bool enable) {
    auto &mem = runtime_.memory();
    if (!saved_prices_) {
        for (std::size_t k = 0; k < kPiercingRecipes.size(); ++k) {
            const std::uint32_t addr = kHeadArmorPriceTable + static_cast<std::uint32_t>(kPiercingRecipes[k].equip_id) * kHeadArmorStride;
            if (mem.contains(addr, 4u)) original_prices_[k] = mem.load32(addr);
        }
        saved_prices_ = true;
    }
    for (std::size_t k = 0; k < kPiercingRecipes.size(); ++k) {
        const std::uint32_t addr = kHeadArmorPriceTable + static_cast<std::uint32_t>(kPiercingRecipes[k].equip_id) * kHeadArmorStride;
        if (mem.contains(addr, 4u)) mem.store32(addr, enable ? kPiercingRawPrice : original_prices_[k]);
    }
}

void CraftablePiercingsMod::ensure_guest_table() {
    auto &mem = runtime_.memory();
    for (std::size_t k = 0; k < kPiercingRecipes.size(); ++k) {
        const auto &r = kPiercingRecipes[k];
        const std::uint32_t addr = kGuestRecipeBuffer + static_cast<std::uint32_t>(k) * kRecipeStride;
        mem.store8(addr + 0u, 1u); // Head armor
        mem.store8(addr + 1u, 0u);
        mem.store16(addr + 2u, r.equip_id);
        for (std::size_t m = 0; m < 4; ++m) {
            mem.store16(addr + 4u + static_cast<std::uint32_t>(m) * 4u, r.mats[m].item_id);
            mem.store16(addr + 6u + static_cast<std::uint32_t>(m) * 4u, r.mats[m].qty);
        }
        mem.store32(addr + 20u, 0u);
        mem.store16(addr + 24u, static_cast<std::uint16_t>(kBaseRecipeIndex + k));
    }
}

void CraftablePiercingsMod::on_forge_recipe_unlock_ptr(psp::Runtime &, psp::Context &ctx) {
    if (!enabled_.load()) return;
    if ((ctx.gpr[5] & 0xFFFFu) != 0u) return; // armor recipes only (kind == 0)
    const std::uint32_t idx = ctx.gpr[6] & 0xFFFFu;
    if (idx < kBaseRecipeIndex || idx >= kBaseRecipeIndex + kPiercingRecipes.size()) return;
    ensure_guest_table();
    ctx.gpr[30] = kGuestRecipeBuffer + (idx - kBaseRecipeIndex) * kRecipeStride;
}

void CraftablePiercingsMod::on_smithy_create_list_populate_end(psp::Runtime &rt, psp::Context &ctx) {
    if (!enabled_.load() || !is_village_overlay(rt, 0x09AA2C44u)) return;
    auto &mem = rt.memory();
    const std::uint32_t list = ctx.gpr[21];
    if (mem.load8(list + 37u) != 0u) return; // decorations use a separate table
    if (ctx.gpr[22] != 1u) return;           // Head armor category only

    const std::uint32_t game_sys = mem.load32(*region::mh_game_sys);
    if (game_sys == 0u) return;
    ensure_guest_table();
    apply_prices(true);

    for (std::size_t k = 0; k < kPiercingRecipes.size(); ++k) {
        const std::uint16_t equip_id = kPiercingRecipes[k].equip_id;
        const std::uint16_t recipe_idx = static_cast<std::uint16_t>(kBaseRecipeIndex + k);

        psp::Context call = ctx;
        call.gpr[4] = game_sys;
        call.gpr[5] = 1u;
        call.gpr[6] = equip_id;
        rt.call(call, *region::mh_armor_price);
        const std::uint32_t price = call.gpr[2];

        call = ctx;
        call.gpr[4] = game_sys;
        call.gpr[5] = 1u;
        call.gpr[6] = equip_id;
        rt.call(call, *region::mh_armor_owned_check);
        const std::uint32_t owned = static_cast<std::uint32_t>(static_cast<std::int32_t>(call.gpr[2]) >> 1);

        call = ctx;
        call.gpr[4] = game_sys;
        call.gpr[5] = 0u;
        call.gpr[6] = recipe_idx;
        call.gpr[7] = 0u;
        rt.call(call, *region::mh_forge_materials_check);
        const bool has_mats = (call.gpr[2] & 0xFFu) != 0u;

        const std::uint32_t slot = ctx.gpr[19];
        mem.store32(slot + 0u, owned);
        mem.store32(slot + 4u, price);
        mem.store16(slot + 8u, has_mats ? 0u : 1u);
        mem.store16(slot + 10u, 1u); // Head
        mem.store16(slot + 12u, equip_id);
        mem.store16(slot + 14u, recipe_idx);

        ctx.gpr[19] += 16u;
        ctx.gpr[17] += 1u;
    }
}

void CraftablePiercingsMod::on_smithy_panel_recipe_ptr(psp::Runtime &rt, psp::Context &ctx) {
    if (!enabled_.load() || !is_village_overlay(rt, 0x09A83D50u)) return;
    auto &mem = rt.memory();
    if (mem.load8(ctx.gpr[16] + 37u) != 0u) return;
    const std::uint32_t recipe_idx = mem.load16(ctx.gpr[17] + 14u);
    if (recipe_idx < kBaseRecipeIndex || recipe_idx >= kBaseRecipeIndex + kPiercingRecipes.size()) return;
    ensure_guest_table();
    ctx.gpr[18] = kGuestRecipeBuffer + (recipe_idx - kBaseRecipeIndex) * kRecipeStride;
}

void CraftablePiercingsMod::on_smithy_craft_consume_ptr(psp::Runtime &rt, psp::Context &ctx) {
    if (!enabled_.load() || !is_village_overlay(rt, 0x09AA5C10u)) return;
    if (ctx.gpr[16] != 0u) return; // only redirect before slot 0; slots 1..3 advance ctx.gpr[18] += 4
    const std::uint32_t recipe_idx = ctx.gpr[5] & 0xFFFFu;
    if (recipe_idx < kBaseRecipeIndex || recipe_idx >= kBaseRecipeIndex + kPiercingRecipes.size()) return;
    ensure_guest_table();
    ctx.gpr[18] = kGuestRecipeBuffer + (recipe_idx - kBaseRecipeIndex) * kRecipeStride;
}

} // namespace mhf::mhfu
