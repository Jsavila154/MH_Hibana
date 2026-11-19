#include "mhf/psp/patch_engine.hpp"

#include <bit>
#include <cstring>

namespace mhf::psp {

namespace {

std::size_t patch_byte_size(const core::mods::PatchSpec &spec) noexcept {
    switch (spec.kind) {
    case core::mods::PatchValueKind::U8: return 1u;
    case core::mods::PatchValueKind::U16: return 2u;
    case core::mods::PatchValueKind::U32:
    case core::mods::PatchValueKind::F32:
    case core::mods::PatchValueKind::F32Scale: return 4u;
    case core::mods::PatchValueKind::Bytes: return spec.bytes_value.size();
    }
    return 0u;
}

} // namespace

bool PatchEngine::apply_entry(Entry &e) {
    const std::size_t size = patch_byte_size(e.spec);
    if (size == 0u || e.spec.address == 0u) return false;
    if (!memory_.contains(e.spec.address, static_cast<std::uint32_t>(size))) return false;

    std::vector<std::uint8_t> current(size);
    for (std::size_t i = 0; i < size; ++i) {
        current[i] = memory_.load8(e.spec.address + static_cast<std::uint32_t>(i));
    }

    if (!e.applied) {
        if (!e.spec.expected.empty()) {
            if (e.spec.expected.size() != size) return false;
            if (std::memcmp(current.data(), e.spec.expected.data(), size) != 0) return false;
        }
        e.original = std::move(current);
    }

    switch (e.spec.kind) {
    case core::mods::PatchValueKind::U8:
        memory_.store8(e.spec.address, static_cast<std::uint8_t>(e.spec.int_value & 0xFFu));
        break;
    case core::mods::PatchValueKind::U16:
        memory_.store16(e.spec.address, static_cast<std::uint16_t>(e.spec.int_value & 0xFFFFu));
        break;
    case core::mods::PatchValueKind::U32:
        memory_.store32(e.spec.address, e.spec.int_value);
        break;
    case core::mods::PatchValueKind::F32:
        memory_.store32(e.spec.address, std::bit_cast<std::uint32_t>(e.spec.float_value));
        break;
    case core::mods::PatchValueKind::F32Scale: {
        std::uint32_t orig_bits = 0;
        std::memcpy(&orig_bits, e.original.data(), 4u);
        const float scaled = std::bit_cast<float>(orig_bits) * e.spec.float_value;
        memory_.store32(e.spec.address, std::bit_cast<std::uint32_t>(scaled));
        break;
    }
    case core::mods::PatchValueKind::Bytes:
        for (std::size_t i = 0; i < size; ++i) {
            memory_.store8(e.spec.address + static_cast<std::uint32_t>(i), e.spec.bytes_value[i]);
        }
        break;
    }
    e.applied = true;
    return true;
}

void PatchEngine::revert_entry(Entry &e) {
    if (!e.applied || e.original.empty()) return;
    if (memory_.contains(e.spec.address, static_cast<std::uint32_t>(e.original.size()))) {
        for (std::size_t i = 0; i < e.original.size(); ++i) {
            memory_.store8(e.spec.address + static_cast<std::uint32_t>(i), e.original[i]);
        }
    }
    e.applied = false;
}

bool PatchEngine::apply_group(std::string_view group_id, const std::vector<core::mods::PatchSpec> &specs) {
    auto &entries = groups_[std::string(group_id)];
    entries.clear();
    entries.reserve(specs.size());
    for (const auto &s : specs) {
        Entry e;
        e.spec = s;
        if (s.overlay.empty()) {
            if (!apply_entry(e)) {
                revert_group(group_id);
                return false;
            }
        }
        entries.push_back(std::move(e));
    }
    return true;
}

bool PatchEngine::revert_group(std::string_view group_id) {
    const auto it = groups_.find(std::string(group_id));
    if (it == groups_.end()) return false;
    for (Entry &e : it->second) revert_entry(e);
    groups_.erase(it);
    return true;
}

void PatchEngine::on_overlay_loaded(std::string_view overlay_name) {
    for (auto &[id, entries] : groups_) {
        (void)id;
        for (Entry &e : entries) {
            if (!e.spec.overlay.empty() && e.spec.overlay == overlay_name) {
                e.applied = false; // Overlay memory was refreshed from ISO; re-verify and reapply.
                apply_entry(e);
            }
        }
    }
}

bool PatchEngine::is_group_applied(std::string_view group_id) const noexcept {
    const auto it = groups_.find(std::string(group_id));
    return it != groups_.end() && !it->second.empty();
}

bool DataMod::set_enabled(bool enabled) {
    if (enabled == enabled_) return true;
    if (enabled) {
        if (!engine_.apply_group(manifest_.info.id, manifest_.patches)) {
            manifest_.info.status_note = "Patch verification failed (wrong region or address).";
            return false;
        }
        manifest_.info.status_note.clear();
        enabled_ = true;
        return true;
    }
    engine_.revert_group(manifest_.info.id);
    enabled_ = false;
    return true;
}

void DataMod::on_overlay_loaded(std::string_view overlay_name) {
    if (enabled_) engine_.on_overlay_loaded(overlay_name);
}

} // namespace mhf::psp
