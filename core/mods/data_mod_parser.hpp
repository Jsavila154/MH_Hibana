#pragma once

#include "mods/mod.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mhf::core::mods {

enum class PatchValueKind {
    U8,
    U16,
    U32,
    F32,
    F32Scale,
    Bytes,
};

struct PatchSpec {
    std::string overlay;
    std::string symbol;
    std::uint32_t address{};
    PatchValueKind kind{PatchValueKind::U32};
    std::vector<std::uint8_t> expected;
    std::vector<std::uint8_t> bytes_value;
    std::uint32_t int_value{};
    float float_value{};
};

struct DataModManifest {
    ModInfo info;
    std::string region;
    std::vector<PatchSpec> patches;
};

[[nodiscard]] bool parse_data_mod_toml(const std::filesystem::path &path, DataModManifest &out, std::string &error);

} // namespace mhf::core::mods
