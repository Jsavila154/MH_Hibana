#pragma once

#include "mhf/psp/runtime.hpp"
#include "mods/mod.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace mhf::mhfu {

// Makes the 10 Training School Piercings (Head armor IDs 240..249) craftable at the Smithy for 30,000z each.
class CraftablePiercingsMod final : public core::mods::IMod {
public:
    explicit CraftablePiercingsMod(psp::Runtime &runtime);
    ~CraftablePiercingsMod() override;

    [[nodiscard]] const core::mods::ModInfo &info() const noexcept override { return info_; }
    [[nodiscard]] bool is_enabled() const noexcept override { return enabled_.load(); }
    bool set_enabled(bool enabled) override;

    void on_forge_recipe_unlock_ptr(psp::Runtime &rt, psp::Context &ctx);
    void on_smithy_create_list_populate_end(psp::Runtime &rt, psp::Context &ctx);
    void on_smithy_panel_recipe_ptr(psp::Runtime &rt, psp::Context &ctx);
    void on_smithy_craft_consume_ptr(psp::Runtime &rt, psp::Context &ctx);

private:
    void ensure_guest_table();
    void apply_prices(bool enable);

    psp::Runtime &runtime_;
    core::mods::ModInfo info_;
    std::atomic<bool> enabled_{false};
    std::array<std::uint32_t, 10> original_prices_{};
    bool saved_prices_{false};
};

} // namespace mhf::mhfu
