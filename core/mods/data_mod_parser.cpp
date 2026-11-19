#include "mods/data_mod_parser.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string_view>

namespace mhf::core::mods {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string strip_comment(std::string_view line) {
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) in_quotes = !in_quotes;
        else if (line[i] == '#' && !in_quotes) return std::string(line.substr(0, i));
    }
    return std::string(line);
}

std::string unquote(std::string_view s) {
    std::string t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') return t.substr(1, t.size() - 2);
    return t;
}

bool parse_bool(std::string_view s) {
    const std::string t = trim(s);
    return t == "true" || t == "1";
}

bool parse_hex_bytes(std::string_view s, std::vector<std::uint8_t> &out) {
    out.clear();
    std::string hex;
    hex.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == '"') continue;
        if (c == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
            ++i;
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        hex.push_back(c);
    }
    if (hex.empty() || (hex.size() & 1u) != 0u) return false;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const std::string byte_str = hex.substr(i, 2);
        out.push_back(static_cast<std::uint8_t>(std::strtoul(byte_str.c_str(), nullptr, 16)));
    }
    return true;
}

PatchValueKind parse_kind(std::string_view s) {
    const std::string k = unquote(s);
    if (k == "u8") return PatchValueKind::U8;
    if (k == "u16") return PatchValueKind::U16;
    if (k == "u32") return PatchValueKind::U32;
    if (k == "f32") return PatchValueKind::F32;
    if (k == "f32_scale") return PatchValueKind::F32Scale;
    return PatchValueKind::Bytes;
}

ModCategory parse_category(std::string_view s) {
    const std::string c = unquote(s);
    if (c == "cosmetic") return ModCategory::Cosmetic;
    if (c == "qol" || c == "quality_of_life") return ModCategory::QualityOfLife;
    return ModCategory::Gameplay;
}

} // namespace

bool parse_data_mod_toml(const std::filesystem::path &path, DataModManifest &out, std::string &error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open mod manifest";
        return false;
    }
    out = DataModManifest{};
    bool in_patch = false;
    std::string line;
    while (std::getline(in, line)) {
        const std::string clean = trim(strip_comment(line));
        if (clean.empty()) continue;
        if (clean == "[[patch]]") {
            in_patch = true;
            out.patches.emplace_back();
            continue;
        }
        if (clean.front() == '[') {
            in_patch = false;
            continue;
        }
        const auto eq = clean.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(clean.substr(0, eq));
        const std::string val = trim(clean.substr(eq + 1));

        if (!in_patch) {
            if (key == "id") out.info.id = unquote(val);
            else if (key == "name") out.info.name = unquote(val);
            else if (key == "description") out.info.description = unquote(val);
            else if (key == "category") out.info.category = parse_category(val);
            else if (key == "region") out.region = unquote(val);
            else if (key == "requires_zone_reload") out.info.requires_zone_reload = parse_bool(val);
            else if (key == "online_safe") out.info.online_safe = parse_bool(val);
        } else {
            PatchSpec &p = out.patches.back();
            if (key == "overlay") p.overlay = unquote(val);
            else if (key == "symbol") p.symbol = unquote(val);
            else if (key == "address") p.address = static_cast<std::uint32_t>(std::strtoul(unquote(val).c_str(), nullptr, 0));
            else if (key == "type") p.kind = parse_kind(val);
            else if (key == "expected") {
                if (!parse_hex_bytes(val, p.expected)) {
                    error = "invalid expected hex bytes";
                    return false;
                }
            } else if (key == "value") {
                if (p.kind == PatchValueKind::F32 || p.kind == PatchValueKind::F32Scale) {
                    p.float_value = std::strtof(unquote(val).c_str(), nullptr);
                } else if (p.kind == PatchValueKind::Bytes) {
                    if (!parse_hex_bytes(val, p.bytes_value)) {
                        error = "invalid bytes value";
                        return false;
                    }
                } else {
                    p.int_value = static_cast<std::uint32_t>(std::strtoul(unquote(val).c_str(), nullptr, 0));
                }
            }
        }
    }
    if (out.info.id.empty()) {
        error = "missing mod id";
        return false;
    }
    if (out.info.name.empty()) out.info.name = out.info.id;
    return true;
}

} // namespace mhf::core::mods
