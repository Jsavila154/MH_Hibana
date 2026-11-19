#pragma once

#include "mhf/psp/runtime.hpp"
#include "mods/mod.hpp"

#include <atomic>

namespace mhf::mhfu {

// Diagnostic/testing sandbox mod: unlocks all Smithy recipes and lets you craft without spending materials or zenny.
class DevSandboxMod final : public core::mods::IMod {
public:
    explicit DevSandboxMod(psp::Runtime &runtime);
    ~DevSandboxMod() override;

    [[nodiscard]] const core::mods::ModInfo &info() const noexcept override { return info_; }
    [[nodiscard]] bool is_enabled() const noexcept override { return enabled_.load(); }
    bool set_enabled(bool enabled) override;

    void on_forge_unlock(psp::Runtime &rt, psp::Context &ctx);
    void on_zenny_check(psp::Runtime &rt, psp::Context &ctx);
    void on_free_craft(psp::Runtime &rt, psp::Context &ctx);

private:
    psp::Runtime &runtime_;
    core::mods::ModInfo info_;
    std::atomic<bool> enabled_{false};
};

} // namespace mhf::mhfu
