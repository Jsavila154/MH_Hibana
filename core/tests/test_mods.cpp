#include "mods/data_mod_parser.hpp"
#include "mods/mod.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {

class DummyMod final : public mhf::core::mods::IMod {
public:
    DummyMod(std::string id, std::string name, bool online_safe) {
        info_.id = std::move(id);
        info_.name = std::move(name);
        info_.online_safe = online_safe;
    }
    const mhf::core::mods::ModInfo &info() const noexcept override { return info_; }
    bool is_enabled() const noexcept override { return enabled_; }
    bool set_enabled(bool enabled) override {
        enabled_ = enabled;
        return true;
    }

private:
    mhf::core::mods::ModInfo info_;
    bool enabled_{false};
};

} // namespace

int main() {
    using namespace mhf::core::mods;

    ModRegistry reg;
    reg.register_mod(std::make_unique<DummyMod>("frontier_evade", "Frontier Evade", false));
    reg.register_mod(std::make_unique<DummyMod>("damage_numbers", "Damage Numbers", true));

    assert(reg.set_enabled("frontier_evade", true));
    assert(reg.set_enabled("damage_numbers", true));
    assert(reg.find("frontier_evade")->is_enabled());
    assert(reg.find("damage_numbers")->is_enabled());

    // Entering online suspends non-online-safe gameplay mods and blocks enabling them.
    reg.set_online_active(true);
    assert(!reg.find("frontier_evade")->is_enabled());
    assert(reg.find("damage_numbers")->is_enabled());
    assert(!reg.set_enabled("frontier_evade", true));

    // Persistence preserves the user's choice even while suspended for online.
    const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "mhf_test_mods";
    std::filesystem::create_directories(tmp_dir);
    const std::filesystem::path cfg = tmp_dir / "mods.cfg";
    assert(reg.save(cfg));

    // Leaving online restores suspended gameplay mods.
    reg.set_online_active(false);
    assert(reg.find("frontier_evade")->is_enabled());

    // Reloading from disk restores saved toggles.
    ModRegistry reg2;
    reg2.register_mod(std::make_unique<DummyMod>("frontier_evade", "Frontier Evade", false));
    reg2.register_mod(std::make_unique<DummyMod>("damage_numbers", "Damage Numbers", true));
    assert(reg2.load(cfg));
    assert(reg2.find("frontier_evade")->is_enabled());
    assert(reg2.find("damage_numbers")->is_enabled());

    // Parse a data mod manifest.
    const std::filesystem::path toml_path = tmp_dir / "mod.toml";
    {
        std::ofstream out(toml_path, std::ios::trunc);
        out << "id = \"test_reach\"\n"
            << "name = \"Test Reach\"\n"
            << "description = \"Scales weapon reach\"\n"
            << "category = \"gameplay\"\n"
            << "region = \"ULES01213\"\n"
            << "requires_zone_reload = true\n"
            << "online_safe = false\n"
            << "[[patch]]\n"
            << "overlay = \"game_task.ovl\"\n"
            << "address = \"0x09A60000\"\n"
            << "type = \"f32_scale\"\n"
            << "expected = \"00 00 80 3F\"\n"
            << "value = 1.25\n";
    }
    DataModManifest manifest;
    std::string error;
    assert(parse_data_mod_toml(toml_path, manifest, error));
    assert(manifest.info.id == "test_reach");
    assert(manifest.info.requires_zone_reload);
    assert(!manifest.info.online_safe);
    assert(manifest.patches.size() == 1u);
    assert(manifest.patches[0].overlay == "game_task.ovl");
    assert(manifest.patches[0].address == 0x09A60000u);
    assert(manifest.patches[0].kind == PatchValueKind::F32Scale);
    assert(manifest.patches[0].expected.size() == 4u);
    assert(manifest.patches[0].float_value == 1.25f);

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    std::printf("[test_mods] all assertions passed\n");
    return 0;
}
