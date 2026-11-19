#include "ge.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace mhf::psp::gpu {

namespace {

enum : std::uint32_t {
    kNop = 0x00, kVaddr = 0x01, kIaddr = 0x02, kPrim = 0x04, kBezier = 0x05, kSpline = 0x06, kBoundingBox = 0x07,
    kJump = 0x08, kBJump = 0x09, kCall = 0x0A, kRet = 0x0B, kEnd = 0x0C, kSignal = 0x0E, kFinish = 0x0F,
    kBase = 0x10, kVertexType = 0x12, kOffsetAddr = 0x13, kOrigin = 0x14,
    kLightingEnable = 0x17, kLightEnable0 = 0x18, kCullFaceEnable = 0x1D, kTextureMapEnable = 0x1E,
    kFogEnable = 0x1F, kAlphaBlendEnable = 0x21, kAlphaTestEnable = 0x22, kZTestEnable = 0x23,
    kStencilTestEnable = 0x24, kColorTestEnable = 0x27, kLogicOpEnable = 0x28,
    kBoneMatrixNumber = 0x2A, kBoneMatrixData = 0x2B, kMorphWeight0 = 0x2C,
    kWorldMatrixNumber = 0x3A, kWorldMatrixData = 0x3B, kViewMatrixNumber = 0x3C, kViewMatrixData = 0x3D,
    kProjMatrixNumber = 0x3E, kProjMatrixData = 0x3F, kTgenMatrixNumber = 0x40, kTgenMatrixData = 0x41,
    kViewportXScale = 0x42, kViewportYScale = 0x43, kViewportZScale = 0x44,
    kViewportXCenter = 0x45, kViewportYCenter = 0x46, kViewportZCenter = 0x47,
    kTexScaleU = 0x48, kTexScaleV = 0x49, kTexOffsetU = 0x4A, kTexOffsetV = 0x4B, kOffsetX = 0x4C, kOffsetY = 0x4D,
    kShadeMode = 0x50, kReverseNormal = 0x51, kMaterialUpdate = 0x53, kMaterialEmissive = 0x54,
    kMaterialAmbient = 0x55, kMaterialDiffuse = 0x56, kMaterialSpecular = 0x57, kMaterialAlpha = 0x58,
    kMaterialSpecularCoef = 0x5B, kAmbientColor = 0x5C, kAmbientAlpha = 0x5D, kLightMode = 0x5E,
    kLightType0 = 0x5F, kLightPos0 = 0x63, kLightDir0 = 0x6F, kLightAtt0 = 0x7B, kLightSpotExp0 = 0x87,
    kLightSpotCutoff0 = 0x8B, kLightColor0 = 0x8F,
    kCull = 0x9B, kFramebufPtr = 0x9C, kFramebufWidth = 0x9D, kZbufPtr = 0x9E, kZbufWidth = 0x9F,
    kTexAddr0 = 0xA0, kTexBufWidth0 = 0xA8, kClutAddr = 0xB0, kClutAddrUpper = 0xB1,
    kTransferSrc = 0xB2, kTransferSrcW = 0xB3, kTransferDst = 0xB4, kTransferDstW = 0xB5, kTexSize0 = 0xB8,
    kTexMapMode = 0xC0, kTexShadeLs = 0xC1, kTexMode = 0xC2, kTexFormat = 0xC3, kLoadClut = 0xC4,
    kClutFormat = 0xC5, kTexFilter = 0xC6, kTexWrap = 0xC7, kTexFunc = 0xC9, kTexEnvColor = 0xCA,
    kFog1 = 0xCD, kFog2 = 0xCE, kFogColor = 0xCF, kFramebufPixFormat = 0xD2, kClearMode = 0xD3,
    kScissor1 = 0xD4, kScissor2 = 0xD5, kMinZ = 0xD6, kMaxZ = 0xD7, kColorTest = 0xD8, kColorRef = 0xD9,
    kColorTestMask = 0xDA, kAlphaTest = 0xDB, kStencilTest = 0xDC, kStencilOp = 0xDD, kZTest = 0xDE,
    kBlendMode = 0xDF, kBlendFixedA = 0xE0, kBlendFixedB = 0xE1, kLogicOp = 0xE6, kZWriteDisable = 0xE7,
    kMaskRgb = 0xE8, kMaskAlpha = 0xE9, kTransferStart = 0xEA, kTransferSrcPos = 0xEB, kTransferDstPos = 0xEC,
    kTransferSize = 0xEE,
};

constexpr std::uint32_t kVram = 0x04000000u;

std::uint32_t pack(float r, float g, float b, float a) {
    auto c = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f) & 0xFFu; };
    return c(r) | (c(g) << 8u) | (c(b) << 16u) | (c(a) << 24u);
}

void unpack(std::uint32_t abgr, float out[4]) {
    out[0] = static_cast<float>(abgr & 0xFFu);
    out[1] = static_cast<float>((abgr >> 8u) & 0xFFu);
    out[2] = static_cast<float>((abgr >> 16u) & 0xFFu);
    out[3] = static_cast<float>(abgr >> 24u);
}

std::uint32_t expand5(std::uint32_t v) { return (v << 3u) | (v >> 2u); }
std::uint32_t expand6(std::uint32_t v) { return (v << 2u) | (v >> 4u); }
std::uint32_t expand4(std::uint32_t v) { return v | (v << 4u); }

// 16-bit color in one of the PSP formats → ABGR8888.
std::uint32_t decode16(std::uint32_t p, std::uint32_t format) {
    switch (format) {
    case 0: return expand5(p & 31u) | (expand6((p >> 5u) & 63u) << 8u) | (expand5((p >> 11u) & 31u) << 16u) | 0xFF000000u;
    case 1: return expand5(p & 31u) | (expand5((p >> 5u) & 31u) << 8u) | (expand5((p >> 10u) & 31u) << 16u) |
                   ((p & 0x8000u) != 0 ? 0xFF000000u : 0u);
    default: return expand4(p & 15u) | (expand4((p >> 4u) & 15u) << 8u) | (expand4((p >> 8u) & 15u) << 16u) |
                    (expand4((p >> 12u) & 15u) << 24u);
    }
}

std::uint32_t encode16(std::uint32_t c, std::uint32_t format) {
    const std::uint32_t r = c & 0xFFu, g = (c >> 8u) & 0xFFu, b = (c >> 16u) & 0xFFu, a = c >> 24u;
    switch (format) {
    case 0: return (r >> 3u) | ((g >> 2u) << 5u) | ((b >> 3u) << 11u);
    case 1: return (r >> 3u) | ((g >> 3u) << 5u) | ((b >> 3u) << 10u) | ((a >= 0x80u) ? 0x8000u : 0u);
    default: return (r >> 4u) | ((g >> 4u) << 4u) | ((b >> 4u) << 8u) | ((a >> 4u) << 12u);
    }
}

void mul43(const Matrix43 &m, const float in[3], float out[3], float w = 1.0f) {
    for (int i = 0; i < 3; ++i) {
        out[i] = in[0] * m.m[static_cast<std::size_t>(i)] + in[1] * m.m[static_cast<std::size_t>(3 + i)] +
                 in[2] * m.m[static_cast<std::size_t>(6 + i)] + w * m.m[static_cast<std::size_t>(9 + i)];
    }
}

void mul44(const Matrix44 &m, const float in[4], float out[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = in[0] * m.m[static_cast<std::size_t>(i)] + in[1] * m.m[static_cast<std::size_t>(4 + i)] +
                 in[2] * m.m[static_cast<std::size_t>(8 + i)] + in[3] * m.m[static_cast<std::size_t>(12 + i)];
    }
}

bool compare(std::uint32_t func, int value, int ref) {
    switch (func & 7u) {
    case 0: return false;
    case 1: return true;
    case 2: return value == ref;
    case 3: return value != ref;
    case 4: return value < ref;
    case 5: return value <= ref;
    case 6: return value > ref;
    default: return value >= ref;
    }
}

Vertex lerp(const Vertex &a, const Vertex &b, float t) {
    auto l = [t](float x, float y) { return x + (y - x) * t; };
    Vertex v;
    v.x = l(a.x, b.x); v.y = l(a.y, b.y); v.z = l(a.z, b.z); v.w = l(a.w, b.w);
    v.u = l(a.u, b.u); v.v = l(a.v, b.v);
    v.r = l(a.r, b.r); v.g = l(a.g, b.g); v.b = l(a.b, b.b); v.a = l(a.a, b.a);
    v.sr = l(a.sr, b.sr); v.sg = l(a.sg, b.sg); v.sb = l(a.sb, b.sb);
    v.fog = l(a.fog, b.fog);
    for (int i = 0; i < 3; ++i) v.world[i] = l(a.world[i], b.world[i]);
    v.has_world = a.has_world && b.has_world;
    return v;
}

} // namespace

float float24(std::uint32_t data) { return std::bit_cast<float>(data << 8u); }

Ge::Ge(psprecomp::GuestMemory &memory) {
    ram_size_ = memory.size();
    ram_ = memory.raw_pointer(psprecomp::GuestMemory::kPhysicalBase, ram_size_);
    vram_ = memory.raw_pointer(psprecomp::GuestMemory::kVramPhysicalBase, psprecomp::GuestMemory::kVramSize);
}

// ---------------------------------------------------------------- display lists

Ge::ListResult Ge::run(std::uint32_t pc, std::uint32_t stall, const ListEvent &on_event) {
    // A list that starts with a table of layers (how Monster Hunter sorts its 2D, DrawInfo::layer): entries of two
    // words, a BASE and the JUMP to what the layer holds, which jumps back to the next entry. The game keeps two such
    // lists, one per flip, and what a layer holds may lie right after the table, so the table is measured: it lasts as
    // long as the words are a BASE and a JUMP.
    layers_start_ = pc;
    layers_end_ = pc;
    while (layers_end_ - pc < kMaxLayers * 8u && (rd32(layers_end_) >> 24u) == kBase && (rd32(layers_end_ + 4u) >> 24u) == kJump) {
        layers_end_ += 8u;
    }
    layer_ = DrawInfo::kNoLayer;
    const ListResult result = run_commands(pc, stall, on_event);
    if (backend_ != nullptr) backend_->flush();
    return result;
}

Ge::ListResult Ge::run_commands(std::uint32_t pc, std::uint32_t stall, const ListEvent &on_event) {
    constexpr std::uint64_t kMaxCommands = 20'000'000u;
    for (std::uint64_t n = 0; n < kMaxCommands; ++n) {
        if (stall != 0 && (pc & 0x0FFFFFFFu) == (stall & 0x0FFFFFFFu)) {
            return {pc, false, true};
        }
        const std::uint32_t word = rd32(pc);
        const std::uint32_t cmd = word >> 24u;
        const std::uint32_t data = word & 0x00FFFFFFu;
        const std::uint32_t here = pc;
        pc += 4u;
        ++stats_.commands;
        auto relative = [&](std::uint32_t d) { return (offset_ + (((base_ & 0x000F0000u) << 8u) | d)) & 0x0FFFFFFFu; };
        switch (cmd) {
        case kJump:
            // The JUMP of an entry of the list's table of layers: what follows is in that layer.
            if (here >= layers_start_ && here < layers_end_ && ((here - layers_start_) & 7u) == 4u) layer_ = (here - layers_start_) / 8u;
            pc = relative(data & 0x00FFFFFCu);
            continue;
        case kBJump: continue; // taken only when the last bounding box was invisible; we draw everything
        case kCall:
            call_stack_.push_back(pc);
            call_stack_.push_back(offset_);
            pc = relative(data & 0x00FFFFFCu);
            continue;
        case kRet:
            if (call_stack_.size() >= 2) {
                offset_ = call_stack_.back();
                call_stack_.pop_back();
                pc = call_stack_.back();
                call_stack_.pop_back();
            }
            continue;
        case kSignal: {
            const std::uint32_t next = rd32(pc);
            if ((next >> 24u) == kEnd) pc += 4u; // SIGNAL is paired with the END that follows it
            if (on_event) on_event(false, data & 0xFFFFu);
            continue;
        }
        case kFinish:
            if (on_event) on_event(true, data & 0xFFFFu);
            continue;
        case kEnd:
            call_stack_.clear();
            return {pc, true, false};
        case kBase: base_ = data; regs_[cmd] = data; continue;
        case kOffsetAddr: offset_ = data << 8u; continue;
        case kOrigin: offset_ = here & 0x0FFFFFFFu; continue;
        case kVaddr: regs_[cmd] = relative(data); continue;
        case kIaddr: regs_[cmd] = relative(data); continue;
        default: execute(word, here); continue;
        }
    }
    std::fprintf(stderr, "[ge] display list did not end within the command limit at %08X\n", pc);
    return {pc, true, false};
}

void Ge::execute(std::uint32_t word, std::uint32_t) {
    const std::uint32_t cmd = word >> 24u;
    const std::uint32_t data = word & 0x00FFFFFFu;
    switch (cmd) {
    case kPrim: regs_[cmd] = data; draw_primitive(data); return;
    case kBezier: case kSpline: draw_bezier_or_spline(cmd, data); return;
    case kBoundingBox: return;
    case kWorldMatrixNumber: world_i_ = data & 15u; return;
    case kWorldMatrixData: if (world_i_ < 12) world_.m[world_i_++] = float24(data); hw_state_dirty_ = true; return;
    case kViewMatrixNumber: view_i_ = data & 15u; return;
    case kViewMatrixData: if (view_i_ < 12) view_.m[view_i_++] = float24(data); ++camera_generation_; hw_state_dirty_ = true; return;
    case kProjMatrixNumber: proj_i_ = data & 15u; return;
    case kProjMatrixData: if (proj_i_ < 16) proj_.m[proj_i_++] = float24(data); ++camera_generation_; hw_state_dirty_ = true; return;
    case kTgenMatrixNumber: texgen_i_ = data & 15u; return;
    case kTgenMatrixData: if (texgen_i_ < 12) texgen_.m[texgen_i_++] = float24(data); hw_state_dirty_ = true; return;
    case kBoneMatrixNumber: bone_i_ = data & 0x7Fu; return;
    case kBoneMatrixData:
        if (bone_i_ < 96) {
            bones_[bone_i_ / 12].m[bone_i_ % 12] = float24(data);
            ++bone_i_;
            hw_state_dirty_ = true;
        }
        return;
    case kLoadClut: load_clut(data); return;
    case kTransferStart: block_transfer(data); return;
    default:
        if (cmd >= kMorphWeight0 && cmd < kMorphWeight0 + 8) morph_[cmd - kMorphWeight0] = float24(data);
        if ((cmd >= kViewportXScale && cmd <= kViewportZCenter) || cmd == kOffsetX || cmd == kOffsetY) ++camera_generation_;
        if (cmd == kVertexType || cmd == kTexMapMode || (cmd >= kLightingEnable && cmd <= 0x9Bu) ||
            cmd == kFog1 || cmd == kFog2) {
            hw_state_dirty_ = true;
        }
        regs_[cmd] = data;
        return;
    }
}

// ---------------------------------------------------------------- vertex decoding

namespace {
constexpr unsigned type_size(unsigned format, unsigned base) noexcept {
    return format == 0 ? 0 : (format == 3 ? 4u : format) * base;
}
constexpr unsigned align_of(unsigned format) noexcept { return format == 3 ? 4u : format; }
} // namespace

Ge::VertexLayout Ge::layout_for_type(std::uint32_t vt) noexcept {
    const unsigned tc = vt & 3u, color = (vt >> 2u) & 7u, normal = (vt >> 5u) & 3u, pos = (vt >> 7u) & 3u;
    const unsigned weight = (vt >> 9u) & 3u, weights = ((vt >> 14u) & 7u) + 1u;
    const unsigned pf = pos != 0 ? pos : 3u;
    VertexLayout l;
    l.weight_step = type_size(weight, 1);
    l.tc_step = type_size(tc, 1);
    l.normal_step = type_size(normal, 1);
    l.pos_step = type_size(pf, 1);
    unsigned offset = 0, biggest = 1;
    auto place = [&](unsigned bytes, unsigned alignment) {
        offset = (offset + alignment - 1u) & ~(alignment - 1u);
        const unsigned at = offset;
        offset += bytes;
        biggest = std::max(biggest, alignment);
        return at;
    };
    if (weight != 0) l.weight_offset = place(l.weight_step * weights, align_of(weight));
    if (tc != 0) l.tc_offset = place(l.tc_step * 2u, align_of(tc));
    if (color != 0) {
        const unsigned bytes = color == 7 ? 4u : 2u;
        l.color_offset = place(bytes, bytes);
    }
    if (normal != 0) l.normal_offset = place(l.normal_step * 3u, align_of(normal));
    l.pos_offset = place(l.pos_step * 3u, align_of(pf));
    l.size = (offset + biggest - 1u) & ~(biggest - 1u);
    return l;
}

std::uint32_t Ge::vertex_size() const {
    const std::uint32_t vt = regs_[kVertexType];
    const unsigned morphs = ((vt >> 18u) & 7u) + 1u;
    return layout_for_type(vt).size * morphs;
}

Ge::DecodedVertex Ge::decode_vertex(std::uint32_t address) const {
    return decode_vertex(address, layout_for_type(regs_[kVertexType]));
}

Ge::DecodedVertex Ge::decode_vertex(std::uint32_t address, const VertexLayout &l) const {
    const std::uint32_t vt = regs_[kVertexType];
    const bool through = (vt & (1u << 23u)) != 0;
    const unsigned tc = vt & 3u, color = (vt >> 2u) & 7u, normal = (vt >> 5u) & 3u, pos = (vt >> 7u) & 3u;
    const unsigned weight = (vt >> 9u) & 3u, weights = ((vt >> 14u) & 7u) + 1u;
    const unsigned morphs = ((vt >> 18u) & 7u) + 1u;
    const unsigned pf = pos != 0 ? pos : 3u;

    DecodedVertex out{};
    if (const std::uint8_t *base = host(address, l.size * morphs); base != nullptr && morphs == 1u) {
        auto ps8 = [](const std::uint8_t *p) { return static_cast<float>(static_cast<std::int8_t>(*p)); };
        auto pu8 = [](const std::uint8_t *p) { return static_cast<float>(*p); };
        auto ps16 = [](const std::uint8_t *p) {
            std::int16_t v;
            std::memcpy(&v, p, 2);
            return static_cast<float>(v);
        };
        auto pu16 = [](const std::uint8_t *p) {
            std::uint16_t v;
            std::memcpy(&v, p, 2);
            return static_cast<float>(v);
        };
        auto pf32 = [](const std::uint8_t *p) {
            float v;
            std::memcpy(&v, p, 4);
            return v;
        };
        if (weight != 0) {
            const std::uint8_t *wp = base + l.weight_offset;
            for (unsigned i = 0; i < weights && i < 8; ++i, wp += l.weight_step) {
                out.weights[i] = weight == 1 ? pu8(wp) * (1.0f / 128.0f)
                               : weight == 2 ? pu16(wp) * (1.0f / 32768.0f)
                                             : pf32(wp);
            }
        }
        if (tc != 0) {
            const std::uint8_t *tp = base + l.tc_offset;
            for (unsigned i = 0; i < 2; ++i, tp += l.tc_step) {
                float t = tc == 1 ? pu8(tp) : tc == 2 ? pu16(tp) : pf32(tp);
                if (!through) t = tc == 1 ? t * (1.0f / 128.0f) : tc == 2 ? t * (1.0f / 32768.0f) : t;
                out.uv[i] = t;
            }
        }
        if (color != 0) {
            const std::uint8_t *cp = base + l.color_offset;
            if (color == 7) {
                std::uint32_t raw;
                std::memcpy(&raw, cp, 4);
                unpack(raw, out.color);
            } else {
                std::uint16_t raw;
                std::memcpy(&raw, cp, 2);
                unpack(decode16(raw, color - 4u), out.color);
            }
            out.has_color = true;
        }
        if (normal != 0) {
            const std::uint8_t *np = base + l.normal_offset;
            for (unsigned i = 0; i < 3; ++i, np += l.normal_step) {
                out.normal[i] = normal == 1 ? ps8(np) * (1.0f / 128.0f)
                              : normal == 2 ? ps16(np) * (1.0f / 32768.0f)
                                            : pf32(np);
            }
            out.has_normal = true;
        }
        const std::uint8_t *pp = base + l.pos_offset;
        for (unsigned i = 0; i < 3; ++i, pp += l.pos_step) {
            if (through) {
                out.pos[i] = pf == 1 ? ps8(pp) : pf == 2 ? (i == 2 ? pu16(pp) : ps16(pp)) : pf32(pp);
            } else {
                out.pos[i] = pf == 1 ? ps8(pp) * (1.0f / 128.0f)
                           : pf == 2 ? ps16(pp) * (1.0f / 32768.0f)
                                     : pf32(pp);
            }
        }
        return out;
    }

    auto s8 = [&](std::uint32_t a) { return static_cast<float>(static_cast<std::int8_t>(rd8(a))); };
    auto u8 = [&](std::uint32_t a) { return static_cast<float>(rd8(a)); };
    auto s16 = [&](std::uint32_t a) { return static_cast<float>(static_cast<std::int16_t>(rd16(a))); };
    auto u16 = [&](std::uint32_t a) { return static_cast<float>(rd16(a)); };
    auto f32 = [&](std::uint32_t a) { return std::bit_cast<float>(rd32(a)); };

    for (unsigned m = 0; m < morphs; ++m) {
        const std::uint32_t v = address + m * l.size;
        const float mw = morphs > 1 ? morph_[m] : 1.0f;
        if (weight != 0) {
            for (unsigned i = 0; i < weights && i < 8; ++i) {
                const std::uint32_t a = v + l.weight_offset + i * l.weight_step;
                out.weights[i] += mw * (weight == 1 ? u8(a) / 128.0f : weight == 2 ? u16(a) / 32768.0f : f32(a));
            }
        }
        if (tc != 0) {
            for (unsigned i = 0; i < 2; ++i) {
                const std::uint32_t a = v + l.tc_offset + i * l.tc_step;
                float t = tc == 1 ? u8(a) : tc == 2 ? u16(a) : f32(a);
                if (!through) t = tc == 1 ? t / 128.0f : tc == 2 ? t / 32768.0f : t;
                out.uv[i] += mw * t;
            }
        }
        if (color != 0) {
            float c[4];
            const std::uint32_t a = v + l.color_offset;
            if (color == 7) unpack(rd32(a), c);
            else unpack(decode16(rd16(a), color - 4u), c);
            for (int i = 0; i < 4; ++i) out.color[i] = (m == 0 ? 0.0f : out.color[i]) + mw * c[i];
            out.has_color = true;
        }
        if (normal != 0) {
            for (unsigned i = 0; i < 3; ++i) {
                const std::uint32_t a = v + l.normal_offset + i * l.normal_step;
                out.normal[i] += mw * (normal == 1 ? s8(a) / 128.0f : normal == 2 ? s16(a) / 32768.0f : f32(a));
            }
            out.has_normal = true;
        }
        for (unsigned i = 0; i < 3; ++i) {
            const std::uint32_t a = v + l.pos_offset + i * l.pos_step;
            float p;
            if (through) {
                p = pf == 1 ? s8(a) : pf == 2 ? (i == 2 ? u16(a) : s16(a)) : f32(a);
            } else {
                p = pf == 1 ? s8(a) / 128.0f : pf == 2 ? s16(a) / 32768.0f : f32(a);
            }
            out.pos[i] += mw * p;
        }
    }
    return out;
}

// ---------------------------------------------------------------- transform and lighting

void Ge::prepare_lighting() {
    LightSetup &s = light_setup_;
    unpack(regs_[kMaterialAmbient] | (regs_[kMaterialAlpha] << 24u), s.ambient);
    unpack(regs_[kMaterialDiffuse], s.diffuse);
    unpack(regs_[kMaterialSpecular], s.specular);
    unpack(regs_[kMaterialEmissive], s.emissive);
    unpack(regs_[kAmbientColor] | (regs_[kAmbientAlpha] << 24u), s.global);
    s.update = regs_[kMaterialUpdate];
    s.reverse_normal = (regs_[kReverseNormal] & 1u) != 0;
    s.shininess = float24(regs_[kMaterialSpecularCoef]);
    s.separate = (regs_[kLightMode] & 1u) != 0;
    for (unsigned i = 0; i < 4; ++i) {
        LightSetup::Light &l = s.lights[i];
        l.on = (regs_[kLightEnable0 + i] & 1u) != 0;
        if (!l.on) continue;
        const std::uint32_t type = regs_[kLightType0 + i];
        l.kind = (type >> 8u) & 3u; // 0 directional, 1 point, 2 spot
        l.comp = type & 3u;         // 0 diffuse, 1 diffuse+specular, 2 powered diffuse
        for (unsigned c = 0; c < 3; ++c) {
            l.lp[c] = float24(regs_[kLightPos0 + i * 3 + c]);
            l.ld[c] = float24(regs_[kLightDir0 + i * 3 + c]);
            l.att[c] = float24(regs_[kLightAtt0 + i * 3 + c]);
        }
        l.cutoff = float24(regs_[kLightSpotCutoff0 + i]);
        l.spot_exp = float24(regs_[kLightSpotExp0 + i]);
        unpack(regs_[kLightColor0 + i * 3 + 0], l.lac);
        unpack(regs_[kLightColor0 + i * 3 + 1], l.ldc);
        unpack(regs_[kLightColor0 + i * 3 + 2], l.lsc);
        std::copy(l.lp, l.lp + 3, l.direction);
        const float ll = std::sqrt(l.direction[0] * l.direction[0] + l.direction[1] * l.direction[1] + l.direction[2] * l.direction[2]);
        if (ll > 0) for (float &c : l.direction) c /= ll;
    }
}

void Ge::light(const float wp[3], const float wn[3], Vertex &out, const DecodedVertex &in) const {
    const LightSetup &s = light_setup_;
    float vc[4] = {in.color[0], in.color[1], in.color[2], in.color[3]};
    const std::uint32_t update = in.has_color ? s.update : 0u;
    float ambient[4], diffuse[4], specular[4];
    std::copy(s.ambient, s.ambient + 4, ambient);
    std::copy(s.diffuse, s.diffuse + 4, diffuse);
    std::copy(s.specular, s.specular + 4, specular);
    const float *emissive = s.emissive, *global = s.global;
    if ((update & 1u) != 0) std::copy(vc, vc + 4, ambient);
    if ((update & 2u) != 0) std::copy(vc, vc + 4, diffuse);
    if ((update & 4u) != 0) std::copy(vc, vc + 4, specular);

    float r = emissive[0] + global[0] * ambient[0] / 255.0f;
    float g = emissive[1] + global[1] * ambient[1] / 255.0f;
    float b = emissive[2] + global[2] * ambient[2] / 255.0f;
    float sr = 0, sg = 0, sb = 0;
    const float a = global[3] * ambient[3] / 255.0f;
    float n[3] = {wn[0], wn[1], wn[2]};
    const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl > 0) for (float &c : n) c /= nl;
    if (s.reverse_normal) for (float &c : n) c = -c;
    const float shininess = s.shininess;

    for (unsigned i = 0; i < 4; ++i) {
        const LightSetup::Light &l = s.lights[i];
        if (!l.on) continue;
        float L[3];
        float attenuation = 1.0f;
        if (l.kind == 0) {
            std::copy(l.direction, l.direction + 3, L);
        } else {
            for (unsigned c = 0; c < 3; ++c) L[c] = l.lp[c] - wp[c];
            const float d = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            const float denom = l.att[0] + l.att[1] * d + l.att[2] * d * d;
            attenuation = denom > 0 ? std::min(1.0f, 1.0f / denom) : 1.0f;
            const float ll = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            if (ll > 0) for (float &c : L) c /= ll;
        }
        if (l.kind == 2) {
            const float *ld = l.ld;
            const float dl = std::sqrt(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
            float spot = dl > 0 ? -(L[0] * ld[0] + L[1] * ld[1] + L[2] * ld[2]) / dl : 0.0f;
            spot = spot >= l.cutoff ? std::pow(std::max(spot, 0.0f), l.spot_exp) : 0.0f;
            attenuation *= spot;
        }
        const float *lac = l.lac, *ldc = l.ldc, *lsc = l.lsc;
        float dot = std::max(0.0f, n[0] * L[0] + n[1] * L[1] + n[2] * L[2]);
        if (l.comp == 2) dot = std::pow(dot, shininess);
        r += attenuation * (lac[0] * ambient[0] + ldc[0] * diffuse[0] * dot) / 255.0f;
        g += attenuation * (lac[1] * ambient[1] + ldc[1] * diffuse[1] * dot) / 255.0f;
        b += attenuation * (lac[2] * ambient[2] + ldc[2] * diffuse[2] * dot) / 255.0f;
        if (l.comp == 1 && dot > 0) {
            float h[3] = {L[0], L[1], L[2] + 1.0f};
            const float hl = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            const float sp = hl > 0 ? std::pow(std::max(0.0f, (n[0] * h[0] + n[1] * h[1] + n[2] * h[2]) / hl), shininess)
                                    : 0.0f;
            sr += attenuation * lsc[0] * specular[0] * sp / 255.0f;
            sg += attenuation * lsc[1] * specular[1] * sp / 255.0f;
            sb += attenuation * lsc[2] * specular[2] * sp / 255.0f;
        }
    }
    if (!s.separate) {
        r += sr; g += sg; b += sb;
        sr = sg = sb = 0;
    }
    out.r = std::min(r, 255.0f);
    out.g = std::min(g, 255.0f);
    out.b = std::min(b, 255.0f);
    out.a = std::min(a, 255.0f);
    out.sr = std::min(sr, 255.0f);
    out.sg = std::min(sg, 255.0f);
    out.sb = std::min(sb, 255.0f);
}

Vertex Ge::transform(const DecodedVertex &in) const {
    const std::uint32_t vt = regs_[kVertexType];
    Vertex out;
    const bool through = (vt & (1u << 23u)) != 0;
    if (through) {
        out.x = in.pos[0];
        out.y = in.pos[1];
        out.z = in.pos[2];
        out.w = 1.0f;
        out.u = in.uv[0];
        out.v = in.uv[1];
        if (in.has_color) {
            out.r = in.color[0]; out.g = in.color[1]; out.b = in.color[2]; out.a = in.color[3];
        } else {
            float m[4];
            unpack(regs_[kMaterialAmbient] | (regs_[kMaterialAlpha] << 24u), m);
            out.r = m[0]; out.g = m[1]; out.b = m[2]; out.a = m[3];
        }
        return out;
    }
    // Skinning.
    float model[3] = {in.pos[0], in.pos[1], in.pos[2]};
    float normal[3] = {in.normal[0], in.normal[1], in.normal[2]};
    if (((vt >> 9u) & 3u) != 0) {
        const unsigned count = ((vt >> 14u) & 7u) + 1u;
        float p[3] = {0, 0, 0}, nn[3] = {0, 0, 0};
        for (unsigned i = 0; i < count; ++i) {
            float tp[3], tn[3];
            mul43(bones_[i], in.pos, tp);
            mul43(bones_[i], in.normal, tn, 0.0f);
            for (int c = 0; c < 3; ++c) {
                p[c] += tp[c] * in.weights[i];
                nn[c] += tn[c] * in.weights[i];
            }
        }
        std::copy(p, p + 3, model);
        std::copy(nn, nn + 3, normal);
    }
    float world[3], view[3], wn[3];
    mul43(world_, model, world);
    mul43(world_, normal, wn, 0.0f);
    mul43(view_, world, view);
    const float v4[4] = {view[0], view[1], view[2], 1.0f};
    float clip[4];
    mul44(proj_, v4, clip);
    out.x = clip[0]; out.y = clip[1]; out.z = clip[2]; out.w = clip[3];
    std::copy(world, world + 3, out.world);
    out.has_world = true;

    // Texture coordinates.
    const std::uint32_t map = regs_[kTexMapMode] & 3u;
    if (map == 1) {
        const std::uint32_t source = (regs_[kTexMapMode] >> 8u) & 3u; // 0 position, 1 uv, 2 normalized normal, 3 normal
        float src[3] = {model[0], model[1], model[2]};
        if (source == 1) { src[0] = in.uv[0]; src[1] = in.uv[1]; src[2] = 0; }
        if (source >= 2) std::copy(in.normal, in.normal + 3, src);
        float t[3];
        mul43(texgen_, src, t);
        out.u = t[2] != 0 ? t[0] / t[2] : t[0];
        out.v = t[2] != 0 ? t[1] / t[2] : t[1];
    } else {
        out.u = in.uv[0] * float24(regs_[kTexScaleU]) + float24(regs_[kTexOffsetU]);
        out.v = in.uv[1] * float24(regs_[kTexScaleV]) + float24(regs_[kTexOffsetV]);
    }

    // Color.
    if ((regs_[kLightingEnable] & 1u) != 0) {
        light(world, wn, out, in);
    } else if (in.has_color) {
        out.r = in.color[0]; out.g = in.color[1]; out.b = in.color[2]; out.a = in.color[3];
    } else {
        float m[4];
        unpack(regs_[kMaterialAmbient] | (regs_[kMaterialAlpha] << 24u), m);
        out.r = m[0]; out.g = m[1]; out.b = m[2]; out.a = m[3];
    }
    // Fog factor from view-space depth.
    if ((regs_[kFogEnable] & 1u) != 0) {
        out.fog = std::clamp((view[2] + float24(regs_[kFog1])) * float24(regs_[kFog2]), 0.0f, 1.0f);
    }
    return out;
}

Vertex Ge::to_screen(const Vertex &c) const {
    Vertex s = c;
    const float inv_w = c.w != 0 ? 1.0f / c.w : 1.0f;
    const float offset_x = static_cast<float>(regs_[kOffsetX] & 0xFFFFu) / 16.0f;
    const float offset_y = static_cast<float>(regs_[kOffsetY] & 0xFFFFu) / 16.0f;
    s.x = c.x * inv_w * float24(regs_[kViewportXScale]) + float24(regs_[kViewportXCenter]) - offset_x;
    s.y = c.y * inv_w * float24(regs_[kViewportYScale]) + float24(regs_[kViewportYCenter]) - offset_y;
    s.z = c.z * inv_w * float24(regs_[kViewportZScale]) + float24(regs_[kViewportZCenter]);
    s.w = inv_w; // keep 1/w for perspective-correct interpolation
    return s;
}

// ---------------------------------------------------------------- primitives

void Ge::draw_primitive(std::uint32_t data) {
    const unsigned type = (data >> 16u) & 7u;
    const unsigned count = data & 0xFFFFu;
    const std::uint32_t vt = regs_[kVertexType];
    const unsigned index_format = (vt >> 11u) & 3u;
    const bool through = (vt & (1u << 23u)) != 0;
    const std::uint32_t vsize = vertex_size();
    std::uint32_t vaddr = regs_[kVaddr];
    std::uint32_t iaddr = regs_[kIaddr];
    ++stats_.primitives;
    stats_.vertices += count;

    if (backend_ != nullptr) {
        DrawInfo draw;
        draw.vertex_address = vaddr;
        draw.index_address = index_format != 0 ? iaddr : 0u;
        draw.vertex_type = vt;
        draw.texture_address = (regs_[kTextureMapEnable] & 1u) != 0
                                   ? (regs_[kTexAddr0] & 0xFFFFF0u) | ((regs_[kTexBufWidth0] & 0x0F0000u) << 8u)
                                   : 0u;
        draw.count = count;
        draw.layer = layer_;
        draw.primitive = static_cast<std::uint8_t>(type);
        draw.through = through;
        draw.clear = (regs_[kClearMode] & 1u) != 0;
        draw.target = regs_[kFramebufPtr] & 0x1FFFF0u;
        draw.world = world_.m;
        draw.view = view_.m;
        draw.projection = proj_.m;
        draw.viewport = {float24(regs_[kViewportXScale]), float24(regs_[kViewportYScale]), float24(regs_[kViewportZScale]),
                         float24(regs_[kViewportXCenter]), float24(regs_[kViewportYCenter]), float24(regs_[kViewportZCenter]),
                         static_cast<float>(regs_[kOffsetX] & 0xFFFFu) / 16.0f, static_cast<float>(regs_[kOffsetY] & 0xFFFFu) / 16.0f};
        backend_->draw_started(draw);

        const unsigned morphs = ((vt >> 18u) & 7u) + 1u;
        if (!through && morphs == 1 && type >= 3u && type <= 5u && count > 0u && backend_->supports_hardware_transform()) {
            if (hw_state_dirty_) {
                if ((regs_[kLightingEnable] & 1u) != 0) prepare_lighting();
                fill_hardware_state(hw_state_, ((vt >> 2u) & 7u) != 0);
                hw_state_.serial = ++hw_state_serial_;
                hw_state_dirty_ = false;
            }
            const VertexLayout hw_layout = layout_for_type(vt);
            auto hash_bytes = [](const std::uint8_t *p, std::uint32_t bytes) -> std::uint64_t {
                std::uint64_t h = 0xCBF29CE484222325ull;
                std::uint32_t i = 0;
                for (; i + 8u <= bytes; i += 8u) {
                    std::uint64_t v;
                    std::memcpy(&v, p + i, 8);
                    h ^= v;
                    h *= 0x100000001B3ull;
                }
                for (; i < bytes; ++i) {
                    h ^= p[i];
                    h *= 0x100000001B3ull;
                }
                return h;
            };
            auto cached_decode = [&](std::uint32_t base_addr, std::uint32_t vcount) -> const HwVertex * {
                const std::uint32_t bytes = vcount * vsize;
                const std::uint8_t *raw = host(base_addr, bytes);
                if (raw == nullptr || vcount > 4096u) return nullptr;
                const std::uint64_t h = hash_bytes(raw, bytes);
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(base_addr) << 32u) ^ (static_cast<std::uint64_t>(vt) * 0x9E3779B97F4A7C15ull) ^ vcount;
                if (hw_mesh_cache_.size() >= 512u && hw_mesh_cache_.find(key) == hw_mesh_cache_.end()) {
                    hw_mesh_cache_.clear();
                }
                CachedHwMesh &entry = hw_mesh_cache_[key];
                entry.last_use = ++hw_mesh_tick_;
                if (entry.verts.size() != vcount || entry.hash != h) {
                    entry.hash = h;
                    entry.verts.resize(vcount);
                    for (std::uint32_t v = 0; v < vcount; ++v) {
                        decode_hw_vertex(base_addr + v * vsize, hw_layout, entry.verts[v]);
                    }
                }
                return entry.verts.data();
            };

            if (index_format == 0u) {
                const HwVertex *vptr = cached_decode(vaddr, count);
                if (vptr == nullptr) {
                    hw_verts_.resize(count);
                    for (unsigned i = 0; i < count; ++i) {
                        decode_hw_vertex(vaddr + i * vsize, hw_layout, hw_verts_[i]);
                    }
                    vptr = hw_verts_.data();
                }
                regs_[kVaddr] = vaddr + count * vsize;
                backend_->draw_hardware(*this, draw, hw_state_, vptr, count, nullptr, 0u);
            } else {
                hw_indices_.resize(count);
                std::uint32_t min_idx = 0xFFFFFFFFu, max_idx = 0u;
                const std::uint32_t idx_bytes = count * (index_format == 1u ? 1u : index_format == 2u ? 2u : 4u);
                if (const std::uint8_t *ip = host(iaddr, idx_bytes)) {
                    for (unsigned i = 0; i < count; ++i) {
                        std::uint32_t idx = 0;
                        if (index_format == 1u) idx = ip[i];
                        else if (index_format == 2u) {
                            std::uint16_t v16;
                            std::memcpy(&v16, ip + i * 2u, 2);
                            idx = v16;
                        } else {
                            std::memcpy(&idx, ip + i * 4u, 4);
                        }
                        hw_indices_[i] = idx;
                        if (idx < min_idx) min_idx = idx;
                        if (idx > max_idx) max_idx = idx;
                    }
                } else {
                    for (unsigned i = 0; i < count; ++i) {
                        std::uint32_t idx = 0;
                        if (index_format == 1u) idx = rd8(iaddr + i);
                        else if (index_format == 2u) idx = rd16(iaddr + i * 2u);
                        else idx = rd32(iaddr + i * 4u);
                        hw_indices_[i] = idx;
                        if (idx < min_idx) min_idx = idx;
                        if (idx > max_idx) max_idx = idx;
                    }
                }
                regs_[kIaddr] = iaddr + idx_bytes;
                const std::uint32_t span = (max_idx >= min_idx) ? (max_idx - min_idx + 1u) : 0u;
                if (span > 0u && span <= count * 2u && span <= 4096u) {
                    const std::uint32_t base_addr = vaddr + min_idx * vsize;
                    const HwVertex *vptr = cached_decode(base_addr, span);
                    if (vptr == nullptr) {
                        hw_verts_.resize(span);
                        for (std::uint32_t v = 0; v < span; ++v) {
                            decode_hw_vertex(base_addr + v * vsize, hw_layout, hw_verts_[v]);
                        }
                        vptr = hw_verts_.data();
                    }
                    if (min_idx != 0u) {
                        for (unsigned i = 0; i < count; ++i) hw_indices_[i] -= min_idx;
                    }
                    backend_->draw_hardware(*this, draw, hw_state_, vptr, span, hw_indices_.data(), count);
                } else {
                    hw_verts_.resize(count);
                    for (unsigned i = 0; i < count; ++i) {
                        decode_hw_vertex(vaddr + hw_indices_[i] * vsize, hw_layout, hw_verts_[i]);
                    }
                    backend_->draw_hardware(*this, draw, hw_state_, hw_verts_.data(), count, nullptr, 0u);
                }
            }
            return;
        }
    }
    // Timing the drawing commands costs a clock read each; only when the interpolation log is on.
    static const bool timed = std::getenv("MHF_INTERP_LOG") != nullptr;
    const auto t_start = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (!through && (regs_[kLightingEnable] & 1u) != 0) prepare_lighting(); // once per draw (see LightSetup)
    const VertexLayout layout = layout_for_type(vt);
    std::vector<Vertex> &verts = verts_;
    verts.clear();

    bool use_indices = false;
    if (index_format == 0) {
        verts.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            const std::uint32_t src_addr = vaddr + i * vsize;
            verts.push_back(transform(decode_vertex(src_addr, layout)));
            verts.back().source = src_addr;
        }
        regs_[kVaddr] = vaddr + count * vsize;
    } else {
        draw_indices_.resize(count);
        std::uint32_t min_idx = 0xFFFFFFFFu, max_idx = 0u;
        const std::uint32_t idx_bytes = count * (index_format == 1 ? 1u : index_format == 2 ? 2u : 4u);
        if (const std::uint8_t *ip = host(iaddr, idx_bytes)) {
            for (unsigned i = 0; i < count; ++i) {
                std::uint32_t idx = 0;
                if (index_format == 1) idx = ip[i];
                else if (index_format == 2) {
                    std::uint16_t v16;
                    std::memcpy(&v16, ip + i * 2u, 2);
                    idx = v16;
                } else {
                    std::memcpy(&idx, ip + i * 4u, 4);
                }
                draw_indices_[i] = idx;
                if (idx < min_idx) min_idx = idx;
                if (idx > max_idx) max_idx = idx;
            }
        } else {
            for (unsigned i = 0; i < count; ++i) {
                std::uint32_t idx = i;
                if (index_format == 1) idx = rd8(iaddr + i);
                else if (index_format == 2) idx = rd16(iaddr + i * 2u);
                else if (index_format == 3) idx = rd32(iaddr + i * 4u);
                draw_indices_[i] = idx;
                if (idx < min_idx) min_idx = idx;
                if (idx > max_idx) max_idx = idx;
            }
        }
        regs_[kIaddr] = iaddr + idx_bytes;
        const std::uint32_t span = (max_idx >= min_idx) ? (max_idx - min_idx + 1u) : 0u;
        if (span > 0u && span <= count && span <= 4096u) {
            use_indices = true;
            index_remap_.assign(span, -1);
            verts.reserve(span);
            for (unsigned i = 0; i < count; ++i) {
                const std::uint32_t rel = draw_indices_[i] - min_idx;
                std::int32_t &slot = index_remap_[rel];
                if (slot < 0) {
                    slot = static_cast<std::int32_t>(verts.size());
                    const std::uint32_t src_addr = vaddr + draw_indices_[i] * vsize;
                    verts.push_back(transform(decode_vertex(src_addr, layout)));
                    verts.back().source = src_addr;
                }
                draw_indices_[i] = static_cast<std::uint32_t>(slot);
            }
        } else {
            verts.reserve(count);
            for (unsigned i = 0; i < count; ++i) {
                const std::uint32_t src_addr = vaddr + draw_indices_[i] * vsize;
                verts.push_back(transform(decode_vertex(src_addr, layout)));
                verts.back().source = src_addr;
            }
        }
    }

    if (!through && type >= 3u && type <= 5u) {
        const std::size_t nverts = verts.size();
        screen_verts_.resize(nverts);
        screen_flags_.resize(nverts);
        const float offset_x = static_cast<float>(regs_[kOffsetX] & 0xFFFFu) / 16.0f;
        const float offset_y = static_cast<float>(regs_[kOffsetY] & 0xFFFFu) / 16.0f;
        const float vpxs = float24(regs_[kViewportXScale]), vpys = float24(regs_[kViewportYScale]),
                    vpzs = float24(regs_[kViewportZScale]);
        const float vpxc = float24(regs_[kViewportXCenter]), vpyc = float24(regs_[kViewportYCenter]),
                    vpzc = float24(regs_[kViewportZCenter]);
        for (std::size_t i = 0; i < nverts; ++i) {
            const Vertex &c = verts[i];
            if (!(c.z > -c.w && c.w > 0.0f)) {
                screen_flags_[i] = 0u;
                continue;
            }
            const float inv_w = 1.0f / c.w;
            const float sx = c.x * inv_w * vpxs + vpxc;
            const float sy = c.y * inv_w * vpys + vpyc;
            if (!(sx >= 0.0f && sx < 4096.0f && sy >= 0.0f && sy < 4096.0f)) {
                screen_flags_[i] = 1u; // inside near plane, outside guard band
                continue;
            }
            Vertex &s = screen_verts_[i];
            s = c;
            s.x = sx - offset_x;
            s.y = sy - offset_y;
            s.z = c.z * inv_w * vpzs + vpzc;
            s.w = inv_w;
            screen_flags_[i] = 3u; // inside near plane and guard band
        }
    }
    const auto t_vertices = timed ? std::chrono::steady_clock::now() : t_start;

    auto v_idx = [&](unsigned i) -> std::uint32_t { return use_indices ? draw_indices_[i] : i; };
    auto tri_idx = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
        if (through) {
            draw_triangle_screen(verts[ia], verts[ib], verts[ic]);
            return;
        }
        const std::uint8_t fa = screen_flags_[ia], fb = screen_flags_[ib], fc = screen_flags_[ic];
        if ((fa & fb & fc) == 3u) {
            if (backend_ != nullptr) backend_->triangle_unclipped(*this, verts[ia], verts[ib], verts[ic]);
            draw_triangle_screen(screen_verts_[ia], screen_verts_[ib], screen_verts_[ic]);
            return;
        }
        if (fa == 1u || fb == 1u || fc == 1u) {
            if (backend_ != nullptr) backend_->triangle_unclipped(*this, verts[ia], verts[ib], verts[ic]);
            return; // outside guard band
        }
        draw_triangle(verts[ia], verts[ib], verts[ic]);
    };
    switch (type) {
    case 0:
        for (unsigned i = 0; i < count; ++i) {
            const Vertex &v = verts[v_idx(i)];
            draw_point(through ? v : to_screen(v));
        }
        break;
    case 1:
        for (unsigned i = 0; i + 1 < count; i += 2) {
            const Vertex &va = verts[v_idx(i)], &vb = verts[v_idx(i + 1)];
            draw_line(through ? va : to_screen(va), through ? vb : to_screen(vb));
        }
        break;
    case 2:
        for (unsigned i = 0; i + 1 < count; ++i) {
            const Vertex &va = verts[v_idx(i)], &vb = verts[v_idx(i + 1)];
            draw_line(through ? va : to_screen(va), through ? vb : to_screen(vb));
        }
        break;
    case 3:
        for (unsigned i = 0; i + 2 < count; i += 3) tri_idx(v_idx(i), v_idx(i + 1), v_idx(i + 2));
        break;
    case 4:
        for (unsigned i = 0; i + 2 < count; ++i) {
            if ((i & 1u) == 0) tri_idx(v_idx(i), v_idx(i + 1), v_idx(i + 2));
            else tri_idx(v_idx(i + 1), v_idx(i), v_idx(i + 2));
        }
        break;
    case 5:
        for (unsigned i = 1; i + 1 < count; ++i) tri_idx(v_idx(0), v_idx(i), v_idx(i + 1));
        break;
    case 6:
        for (unsigned i = 0; i + 1 < count; i += 2) {
            const Vertex &va = verts[v_idx(i)], &vb = verts[v_idx(i + 1)];
            draw_sprite(through ? va : to_screen(va), through ? vb : to_screen(vb));
        }
        break;
    default:
        break;
    }
    if (!timed) return;
    const auto t_end = std::chrono::steady_clock::now();
    stats_.transform_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t_vertices - t_start).count());
    stats_.assemble_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_vertices).count());
}

void Ge::draw_bezier_or_spline(std::uint32_t cmd, std::uint32_t) {
    static bool said = false;
    if (!said) {
        said = true;
        std::fprintf(stderr, "[ge] %s patches are not drawn yet\n", cmd == kBezier ? "Bezier" : "spline");
    }
}

// Like the PSP: a triangle is clipped only against the near plane (clip-space z = -w); any other vertex must land
// inside the GE's 4096x4096 screen coordinate space (before the screen offset), or the whole triangle is dropped.
// Games rely on this culling: the alternative (clipping at w ~ 0) produces huge stretched triangles.
void Ge::draw_triangle(const Vertex &a, const Vertex &b, const Vertex &c) {
    if (backend_ != nullptr) backend_->triangle_unclipped(*this, a, b, c);
    const Vertex *const in[3] = {&a, &b, &c};
    const float offset_x = static_cast<float>(regs_[kOffsetX] & 0xFFFFu) / 16.0f;
    const float offset_y = static_cast<float>(regs_[kOffsetY] & 0xFFFFu) / 16.0f;
    bool any_clipped = false;
    Vertex screen[3]; // each vertex is taken to the screen once: for the guard band test and, unclipped, for drawing
    for (int i = 0; i < 3; ++i) {
        const Vertex &v = *in[i];
        const bool inside = v.z > -v.w && v.w > 0.0f;
        if (!inside) {
            any_clipped = true;
            continue;
        }
        screen[i] = to_screen(v);
        const float sx = screen[i].x + offset_x, sy = screen[i].y + offset_y;
        if (!(sx >= 0.0f && sx < 4096.0f && sy >= 0.0f && sy < 4096.0f)) return; // outside the guard band
    }
    if (!any_clipped) {
        draw_triangle_screen(screen[0], screen[1], screen[2]);
        return;
    }
    Vertex out[4];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const Vertex &p = *in[i];
        const Vertex &q = *in[(i + 1) % 3];
        const float dp = p.z + p.w, dq = q.z + q.w; // signed distance to the near plane
        const bool pin = dp > 0.0f && p.w > 0.0f, qin = dq > 0.0f && q.w > 0.0f;
        if (pin) out[n++] = p;
        if (pin != qin && dp != dq) {
            Vertex v = lerp(p, q, dp / (dp - dq));
            if (v.w <= 0.0f) continue;
            out[n++] = v;
        }
    }
    if (n < 3) return;
    Vertex s[4];
    for (int i = 0; i < n; ++i) s[i] = to_screen(out[i]);
    draw_triangle_screen(s[0], s[1], s[2]);
    if (n == 4) draw_triangle_screen(s[0], s[2], s[3]);
}

void Ge::draw_triangle_screen(const Vertex &a, const Vertex &b, const Vertex &c) {
    if (backend_ != nullptr) {
        backend_->triangle(*this, a, b, c);
        return;
    }
    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area == 0.0f || !std::isfinite(area)) return;
    // Culling is left to depth testing for now; the PSP's face orientation rule needs checking on hardware.
    const int sx1 = static_cast<int>(regs_[kScissor1] & 0x3FFu), sy1 = static_cast<int>((regs_[kScissor1] >> 10u) & 0x3FFu);
    const int sx2 = static_cast<int>(regs_[kScissor2] & 0x3FFu), sy2 = static_cast<int>((regs_[kScissor2] >> 10u) & 0x3FFu);
    const int minx = std::max(sx1, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxx = std::min(sx2, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int miny = std::max(sy1, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxy = std::min(sy2, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (minx > maxx || miny > maxy) return;
    const bool flat = (regs_[kShadeMode] & 1u) == 0;
    const bool through = (regs_[kVertexType] & (1u << 23u)) != 0;
    const float inv_area = 1.0f / area;
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
            float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) * inv_area;
            float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) * inv_area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const float z = w0 * a.z + w1 * b.z + w2 * c.z;
            // Perspective-correct attributes (w holds 1/w; through mode has w = 1).
            float p0 = w0, p1 = w1, p2 = w2;
            if (!through) {
                const float q = w0 * a.w + w1 * b.w + w2 * c.w;
                if (q != 0) {
                    p0 = w0 * a.w / q;
                    p1 = w1 * b.w / q;
                    p2 = w2 * c.w / q;
                }
            }
            const float u = p0 * a.u + p1 * b.u + p2 * c.u;
            const float v = p0 * a.v + p1 * b.v + p2 * c.v;
            float color[4], secondary[3];
            if (flat) {
                color[0] = c.r; color[1] = c.g; color[2] = c.b; color[3] = c.a;
                secondary[0] = c.sr; secondary[1] = c.sg; secondary[2] = c.sb;
            } else {
                color[0] = p0 * a.r + p1 * b.r + p2 * c.r;
                color[1] = p0 * a.g + p1 * b.g + p2 * c.g;
                color[2] = p0 * a.b + p1 * b.b + p2 * c.b;
                color[3] = p0 * a.a + p1 * b.a + p2 * c.a;
                secondary[0] = p0 * a.sr + p1 * b.sr + p2 * c.sr;
                secondary[1] = p0 * a.sg + p1 * b.sg + p2 * c.sg;
                secondary[2] = p0 * a.sb + p1 * b.sb + p2 * c.sb;
            }
            const float fog = p0 * a.fog + p1 * b.fog + p2 * c.fog;
            shade_pixel(x, y, z, u, v, color, secondary, fog);
        }
    }
}

void Ge::draw_sprite(const Vertex &a, const Vertex &b) {
    if (backend_ != nullptr) {
        backend_->sprite(*this, a, b);
        return;
    }
    // Rectangle from the top-left vertex to the bottom-right one, with b's color; texture coordinates
    // interpolate linearly across it.
    const int sx1 = static_cast<int>(regs_[kScissor1] & 0x3FFu), sy1 = static_cast<int>((regs_[kScissor1] >> 10u) & 0x3FFu);
    const int sx2 = static_cast<int>(regs_[kScissor2] & 0x3FFu), sy2 = static_cast<int>((regs_[kScissor2] >> 10u) & 0x3FFu);
    const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    const float u0 = a.x <= b.x ? a.u : b.u, u1 = a.x <= b.x ? b.u : a.u;
    const float v0 = a.y <= b.y ? a.v : b.v, v1 = a.y <= b.y ? b.v : a.v;
    const int ix0 = std::max(sx1, static_cast<int>(std::floor(x0))), ix1 = std::min(sx2, static_cast<int>(std::ceil(x1)) - 1);
    const int iy0 = std::max(sy1, static_cast<int>(std::floor(y0))), iy1 = std::min(sy2, static_cast<int>(std::ceil(y1)) - 1);
    const float color[4] = {b.r, b.g, b.b, b.a};
    const float secondary[3] = {b.sr, b.sg, b.sb};
    const float dx = x1 - x0 != 0 ? x1 - x0 : 1.0f, dy = y1 - y0 != 0 ? y1 - y0 : 1.0f;
    for (int y = iy0; y <= iy1; ++y) {
        const float ty = (static_cast<float>(y) + 0.5f - y0) / dy;
        for (int x = ix0; x <= ix1; ++x) {
            const float tx = (static_cast<float>(x) + 0.5f - x0) / dx;
            shade_pixel(x, y, b.z, u0 + (u1 - u0) * tx, v0 + (v1 - v0) * ty, color, secondary, b.fog);
        }
    }
}

void Ge::draw_line(const Vertex &a, const Vertex &b) {
    if (backend_ != nullptr) {
        // A one-pixel-wide quad along the line.
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0f) return;
        const float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
        Vertex p0 = a, p1 = a, p2 = b, p3 = b;
        for (Vertex *v : {&p0, &p1, &p2, &p3}) v->has_world = false; // offset on screen: cannot be placed again
        p0.x += nx; p0.y += ny; p1.x -= nx; p1.y -= ny;
        p2.x += nx; p2.y += ny; p3.x -= nx; p3.y -= ny;
        backend_->triangle(*this, p0, p1, p2);
        backend_->triangle(*this, p1, p3, p2);
        return;
    }
    const float dx = b.x - a.x, dy = b.y - a.y;
    const int steps = static_cast<int>(std::max(std::fabs(dx), std::fabs(dy)));
    for (int i = 0; i <= steps; ++i) {
        const float t = steps > 0 ? static_cast<float>(i) / static_cast<float>(steps) : 0.0f;
        Vertex p = lerp(a, b, t);
        draw_point(p);
    }
}

void Ge::draw_point(const Vertex &p) {
    if (backend_ != nullptr) {
        Vertex a = p, b = p;
        a.has_world = b.has_world = false;
        b.x += 1.0f;
        b.y += 1.0f;
        backend_->sprite(*this, a, b);
        return;
    }
    const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
    const int sx1 = static_cast<int>(regs_[kScissor1] & 0x3FFu), sy1 = static_cast<int>((regs_[kScissor1] >> 10u) & 0x3FFu);
    const int sx2 = static_cast<int>(regs_[kScissor2] & 0x3FFu), sy2 = static_cast<int>((regs_[kScissor2] >> 10u) & 0x3FFu);
    if (x < sx1 || x > sx2 || y < sy1 || y > sy2) return;
    const float color[4] = {p.r, p.g, p.b, p.a};
    const float secondary[3] = {p.sr, p.sg, p.sb};
    shade_pixel(x, y, p.z, p.u, p.v, color, secondary, p.fog);
}

// ---------------------------------------------------------------- per-pixel pipeline

void Ge::shade_pixel(int x, int y, float zf, float u, float v, const float color_in[4], const float secondary[3],
                     float fog) {
    if (x < 0 || y < 0 || x >= 1024 || y >= 1024) return;
    ++stats_.pixels;
    const int z = static_cast<int>(std::clamp(zf, 0.0f, 65535.0f));
    const std::uint32_t zbuf = kVram | (regs_[kZbufPtr] & 0x1FFFF0u);
    const std::uint32_t zstride = regs_[kZbufWidth] & 0x7FCu;
    const std::uint32_t zaddr = zbuf + (static_cast<std::uint32_t>(y) * zstride + static_cast<std::uint32_t>(x)) * 2u;
    const bool clear = (regs_[kClearMode] & 1u) != 0;

    if (clear) {
        const std::uint32_t c = pack(color_in[0], color_in[1], color_in[2], color_in[3]);
        std::uint32_t mask = 0;
        if ((regs_[kClearMode] & 0x100u) != 0) mask |= 0x00FFFFFFu;
        if ((regs_[kClearMode] & 0x200u) != 0) mask |= 0xFF000000u;
        if (mask != 0) write_pixel(x, y, c, mask);
        if ((regs_[kClearMode] & 0x400u) != 0 && zstride != 0) wr16(zaddr, static_cast<std::uint16_t>(z));
        return;
    }

    // Depth range.
    const int minz = static_cast<int>(regs_[kMinZ] & 0xFFFFu), maxz = static_cast<int>(regs_[kMaxZ] & 0xFFFFu);
    const bool through = (regs_[kVertexType] & (1u << 23u)) != 0;
    if (!through && maxz != 0 && (z < minz || z > maxz)) return;

    float c[4] = {color_in[0], color_in[1], color_in[2], color_in[3]};
    if ((regs_[kTextureMapEnable] & 1u) != 0) {
        float t[4];
        if (through) {
            const std::uint32_t size = regs_[kTexSize0];
            const float w = static_cast<float>(1u << (size & 0xFu)), h = static_cast<float>(1u << ((size >> 8u) & 0xFu));
            sample_texture(u / w, v / h, t);
        } else {
            sample_texture(u, v, t);
        }
        const std::uint32_t func = regs_[kTexFunc];
        const bool rgba = (func & 0x100u) != 0;
        float env[4];
        unpack(regs_[kTexEnvColor], env);
        switch (func & 7u) {
        case 0: // modulate
            for (int i = 0; i < 3; ++i) c[i] = c[i] * t[i] / 255.0f;
            if (rgba) c[3] = c[3] * t[3] / 255.0f;
            break;
        case 1: // decal
            for (int i = 0; i < 3; ++i) c[i] = rgba ? (c[i] * (255.0f - t[3]) + t[i] * t[3]) / 255.0f : t[i];
            break;
        case 2: // blend with the environment color
            for (int i = 0; i < 3; ++i) c[i] = (c[i] * (255.0f - t[i]) + env[i] * t[i]) / 255.0f;
            if (rgba) c[3] = c[3] * t[3] / 255.0f;
            break;
        case 3: // replace
            for (int i = 0; i < 3; ++i) c[i] = t[i];
            if (rgba) c[3] = t[3];
            break;
        default: // add
            for (int i = 0; i < 3; ++i) c[i] = c[i] + t[i];
            if (rgba) c[3] = c[3] * t[3] / 255.0f;
            break;
        }
        if ((func & 0x10000u) != 0) for (int i = 0; i < 3; ++i) c[i] *= 2.0f;
    }
    for (int i = 0; i < 3; ++i) c[i] = std::min(255.0f, c[i] + secondary[i]);
    if ((regs_[kFogEnable] & 1u) != 0 && !through) {
        float fc[4];
        unpack(regs_[kFogColor], fc);
        for (int i = 0; i < 3; ++i) c[i] = c[i] * fog + fc[i] * (1.0f - fog);
    }

    // Alpha test.
    if ((regs_[kAlphaTestEnable] & 1u) != 0) {
        const std::uint32_t at = regs_[kAlphaTest];
        const int mask = static_cast<int>((at >> 16u) & 0xFFu);
        const int ref = static_cast<int>((at >> 8u) & 0xFFu) & mask;
        const int a = static_cast<int>(std::clamp(c[3], 0.0f, 255.0f)) & mask;
        if (!compare(at, a, ref)) return;
    }
    // Depth test.
    if ((regs_[kZTestEnable] & 1u) != 0 && zstride != 0) {
        const int stored = rd16(zaddr);
        if (!compare(regs_[kZTest], z, stored)) return;
        if ((regs_[kZWriteDisable] & 1u) == 0) wr16(zaddr, static_cast<std::uint16_t>(z));
    }

    // Blending.
    if ((regs_[kAlphaBlendEnable] & 1u) != 0) {
        float d[4];
        unpack(read_pixel(x, y), d);
        const std::uint32_t mode = regs_[kBlendMode];
        const float sa = c[3] / 255.0f, da = d[3] / 255.0f;
        auto factor = [&](unsigned f, bool is_a, float out[3]) {
            float fix[4];
            unpack(is_a ? regs_[kBlendFixedA] : regs_[kBlendFixedB], fix);
            const float *other = is_a ? d : c; // "color" factors use the other side's color
            switch (f) {
            case 0: for (int i = 0; i < 3; ++i) out[i] = other[i] / 255.0f; break;
            case 1: for (int i = 0; i < 3; ++i) out[i] = 1.0f - other[i] / 255.0f; break;
            case 2: out[0] = out[1] = out[2] = sa; break;
            case 3: out[0] = out[1] = out[2] = 1.0f - sa; break;
            case 4: out[0] = out[1] = out[2] = da; break;
            case 5: out[0] = out[1] = out[2] = 1.0f - da; break;
            case 6: out[0] = out[1] = out[2] = 2.0f * sa; break;
            case 7: out[0] = out[1] = out[2] = 1.0f - 2.0f * sa; break;
            case 8: out[0] = out[1] = out[2] = 2.0f * da; break;
            case 9: out[0] = out[1] = out[2] = 1.0f - 2.0f * da; break;
            default: for (int i = 0; i < 3; ++i) out[i] = fix[i] / 255.0f; break;
            }
        };
        float fa[3], fb[3];
        factor(mode & 0xFu, true, fa);
        factor((mode >> 4u) & 0xFu, false, fb);
        for (int i = 0; i < 3; ++i) {
            const float s = c[i] * fa[i], dd = d[i] * fb[i];
            switch ((mode >> 8u) & 0xFu) {
            case 0: c[i] = s + dd; break;
            case 1: c[i] = s - dd; break;
            case 2: c[i] = dd - s; break;
            case 3: c[i] = std::min(c[i], d[i]); break;
            case 4: c[i] = std::max(c[i], d[i]); break;
            default: c[i] = std::fabs(c[i] - d[i]); break;
            }
        }
    }
    // The alpha channel holds stencil on the PSP; without stencil emulation keep what the fragment says.
    const std::uint32_t rgb_mask = ~regs_[kMaskRgb] & 0x00FFFFFFu;
    const std::uint32_t a_mask = ((~regs_[kMaskAlpha] & 0xFFu) << 24u);
    const std::uint32_t write_mask = rgb_mask | ((regs_[kStencilTestEnable] & 1u) != 0 ? 0u : a_mask);
    write_pixel(x, y, pack(c[0], c[1], c[2], c[3]), write_mask);
}

// ---------------------------------------------------------------- framebuffer

std::uint32_t Ge::read_pixel(int x, int y) const {
    const std::uint32_t fb = kVram | (regs_[kFramebufPtr] & 0x1FFFF0u);
    const std::uint32_t stride = regs_[kFramebufWidth] & 0x7FCu;
    const std::uint32_t format = regs_[kFramebufPixFormat] & 3u;
    const std::uint32_t index = static_cast<std::uint32_t>(y) * stride + static_cast<std::uint32_t>(x);
    if (format == 3) return rd32(fb + index * 4u);
    return decode16(rd16(fb + index * 2u), format);
}

void Ge::write_pixel(int x, int y, std::uint32_t abgr, std::uint32_t write_mask) {
    const std::uint32_t fb = kVram | (regs_[kFramebufPtr] & 0x1FFFF0u);
    const std::uint32_t stride = regs_[kFramebufWidth] & 0x7FCu;
    if (stride == 0 || static_cast<std::uint32_t>(x) >= stride) return;
    const std::uint32_t format = regs_[kFramebufPixFormat] & 3u;
    const std::uint32_t index = static_cast<std::uint32_t>(y) * stride + static_cast<std::uint32_t>(x);
    if (format == 3) {
        const std::uint32_t a = fb + index * 4u;
        const std::uint32_t old = write_mask == 0xFFFFFFFFu ? 0u : rd32(a);
        wr32(a, (abgr & write_mask) | (old & ~write_mask));
    } else {
        const std::uint32_t a = fb + index * 2u;
        std::uint32_t value = abgr;
        if (write_mask != 0xFFFFFFFFu) {
            const std::uint32_t old = decode16(rd16(a), format);
            value = (abgr & write_mask) | (old & ~write_mask);
        }
        wr16(a, static_cast<std::uint16_t>(encode16(value, format)));
    }
}

void Ge::block_transfer(std::uint32_t data) {
    ++stats_.transfers;
    const std::uint32_t bpp = (data & 1u) != 0 ? 4u : 2u;
    const std::uint32_t src = (regs_[kTransferSrc] & 0xFFFFF0u) | ((regs_[kTransferSrcW] & 0xFF0000u) << 8u);
    const std::uint32_t dst = (regs_[kTransferDst] & 0xFFFFF0u) | ((regs_[kTransferDstW] & 0xFF0000u) << 8u);
    const std::uint32_t src_stride = regs_[kTransferSrcW] & 0x7F8u;
    const std::uint32_t dst_stride = regs_[kTransferDstW] & 0x7F8u;
    const std::uint32_t sx = regs_[kTransferSrcPos] & 0x3FFu, sy = (regs_[kTransferSrcPos] >> 10u) & 0x3FFu;
    const std::uint32_t dx = regs_[kTransferDstPos] & 0x3FFu, dy = (regs_[kTransferDstPos] >> 10u) & 0x3FFu;
    const std::uint32_t w = (regs_[kTransferSize] & 0x3FFu) + 1u, h = ((regs_[kTransferSize] >> 10u) & 0x3FFu) + 1u;
    for (std::uint32_t y = 0; y < h; ++y) {
        const std::uint8_t *from = host(src + ((sy + y) * src_stride + sx) * bpp, w * bpp);
        std::uint8_t *to = host(dst + ((dy + y) * dst_stride + dx) * bpp, w * bpp);
        if (from != nullptr && to != nullptr) std::memmove(to, from, w * bpp);
    }
    if (backend_ != nullptr) backend_->block_transfer(src, src_stride, sx, sy, dst, dst_stride, dx, dy, w, h, bpp);
}

// ---------------------------------------------------------------- textures

void Ge::load_clut(std::uint32_t data) {
    const std::uint32_t address = (regs_[kClutAddr] & 0xFFFFF0u) | ((regs_[kClutAddrUpper] & 0x0F0000u) << 8u);
    const std::uint32_t bytes = std::min<std::uint32_t>((data & 0x3Fu) * 32u, 1024u);
    clut_last_bytes_ = bytes;
    clut_max_bytes_ = std::max(clut_max_bytes_, bytes);
    bool changed = false;
    const std::uint32_t words = bytes / 4u;
    if (const std::uint8_t *p = host(address, bytes)) {
        for (std::uint32_t i = 0; i < words; ++i) {
            std::uint32_t w;
            std::memcpy(&w, p + i * 4u, 4);
            if (clut_[i] != w) {
                clut_[i] = w;
                changed = true;
            }
        }
    } else {
        for (std::uint32_t i = 0; i < words; ++i) {
            const std::uint32_t w = rd32(address + i * 4u);
            if (clut_[i] != w) {
                clut_[i] = w;
                changed = true;
            }
        }
    }
    if (changed || clut_hash_ == 0u) {
        ++clut_generation_;
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (const std::uint32_t c : clut_) {
            h ^= c;
            h *= 0x100000001B3ull;
        }
        clut_hash_ = h;
    }
}

std::uint32_t Ge::clut_lookup(std::uint32_t index) const {
    const std::uint32_t cf = regs_[kClutFormat];
    const std::uint32_t format = cf & 3u;
    const std::uint32_t shift = (cf >> 2u) & 0x1Fu;
    const std::uint32_t mask = (cf >> 8u) & 0xFFu;
    const std::uint32_t start = ((cf >> 16u) & 0x1Fu) * 16u;
    const std::uint32_t i = (((index >> shift) & mask) | start) & 0x1FFu;
    if (format == 3) return clut_[i & 0xFFu];
    const std::uint32_t word = clut_[(i >> 1u) & 0xFFu];
    return decode16((i & 1u) != 0 ? word >> 16u : word & 0xFFFFu, format);
}

std::uint32_t Ge::fetch_texel(int level, int x, int y) const {
    const auto lv = static_cast<std::uint32_t>(level);
    const std::uint32_t addr = (regs_[kTexAddr0 + lv] & 0xFFFFF0u) | ((regs_[kTexBufWidth0 + lv] & 0x0F0000u) << 8u);
    const std::uint32_t stride = regs_[kTexBufWidth0 + lv] & 0x7FFu;
    const std::uint32_t format = regs_[kTexFormat] & 0xFu;
    const bool swizzled = (regs_[kTexMode] & 1u) != 0;
    const auto ux = static_cast<std::uint32_t>(x), uy = static_cast<std::uint32_t>(y);

    // DXT formats: 4x4 blocks, colors first then indices in PSP order.
    if (format >= 8 && format <= 10) {
        const std::uint32_t block_bytes = format == 8 ? 8u : 16u;
        const std::uint32_t blocks_per_row = std::max(1u, stride / 4u);
        const std::uint32_t block = addr + ((uy / 4u) * blocks_per_row + ux / 4u) * block_bytes;
        const std::uint32_t color_block = block + (format == 8 ? 0u : 8u);
        const std::uint32_t lines = rd32(color_block);
        const std::uint32_t c0 = rd16(color_block + 4), c1 = rd16(color_block + 6);
        const std::uint32_t sel = (lines >> (((uy & 3u) * 4u + (ux & 3u)) * 2u)) & 3u;
        float a[4], b[4];
        unpack(decode16(c0, 0), a);
        unpack(decode16(c1, 0), b);
        std::uint32_t out;
        if (sel == 0) out = pack(a[0], a[1], a[2], 255);
        else if (sel == 1) out = pack(b[0], b[1], b[2], 255);
        else if (c0 > c1 || format != 8) {
            const float t = sel == 2 ? 1.0f / 3.0f : 2.0f / 3.0f;
            out = pack(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, 255);
        } else {
            out = sel == 2 ? pack((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, 255) : 0u;
        }
        if (format == 9) {
            const std::uint32_t alpha = (rd16(block + (uy & 3u) * 2u) >> ((ux & 3u) * 4u)) & 0xFu;
            out = (out & 0x00FFFFFFu) | (expand4(alpha) << 24u);
        } else if (format == 10) {
            const std::uint32_t a0 = rd8(block + 6), a1 = rd8(block + 7);
            const std::uint64_t bits = rd32(block) | (static_cast<std::uint64_t>(rd16(block + 4)) << 32u);
            const std::uint32_t code = static_cast<std::uint32_t>(bits >> (((uy & 3u) * 4u + (ux & 3u)) * 3u)) & 7u;
            std::uint32_t alpha;
            if (code == 0) alpha = a0;
            else if (code == 1) alpha = a1;
            else if (a0 > a1) alpha = ((8u - code) * a0 + (code - 1u) * a1) / 7u;
            else alpha = code == 6 ? 0u : code == 7 ? 255u : ((6u - code) * a0 + (code - 1u) * a1) / 5u;
            out = (out & 0x00FFFFFFu) | (alpha << 24u);
        }
        return out;
    }

    // Bits per texel.
    static constexpr std::uint32_t bits_for[8] = {16, 16, 16, 32, 4, 8, 16, 32};
    const std::uint32_t bits = bits_for[format & 7u];
    const std::uint32_t row_bytes = stride * bits / 8u;
    const std::uint32_t xbyte = ux * bits / 8u;
    std::uint32_t offset;
    if (swizzled) {
        const std::uint32_t blocks_per_row = std::max(1u, row_bytes / 16u);
        offset = ((uy / 8u) * blocks_per_row + xbyte / 16u) * 128u + (uy % 8u) * 16u + (xbyte % 16u);
    } else {
        offset = uy * row_bytes + xbyte;
    }
    const std::uint32_t a = addr + offset;
    switch (format) {
    case 0: case 1: case 2: return decode16(rd16(a), format);
    case 3: return rd32(a);
    case 4: return clut_lookup((rd8(a) >> ((ux & 1u) * 4u)) & 0xFu);
    case 5: return clut_lookup(rd8(a));
    case 6: return clut_lookup(rd16(a));
    default: return clut_lookup(rd32(a));
    }
}

void Ge::sample_texture(float u, float v, float out[4]) const {
    const std::uint32_t size = regs_[kTexSize0];
    const int w = 1 << (size & 0xFu), h = 1 << ((size >> 8u) & 0xFu);
    const bool clamp_u = (regs_[kTexWrap] & 1u) != 0, clamp_v = (regs_[kTexWrap] & 0x100u) != 0;
    auto wrap = [](int c, int n, bool clampit) {
        if (clampit) return std::clamp(c, 0, n - 1);
        return c & (n - 1);
    };
    const bool linear = (regs_[kTexFilter] & 0x100u) != 0; // magnification filter
    const float fx = u * static_cast<float>(w) - (linear ? 0.5f : 0.0f);
    const float fy = v * static_cast<float>(h) - (linear ? 0.5f : 0.0f);
    if (!linear) {
        unpack(fetch_texel(0, wrap(static_cast<int>(std::floor(fx)), w, clamp_u),
                           wrap(static_cast<int>(std::floor(fy)), h, clamp_v)), out);
        return;
    }
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    float c00[4], c10[4], c01[4], c11[4];
    unpack(fetch_texel(0, wrap(x0, w, clamp_u), wrap(y0, h, clamp_v)), c00);
    unpack(fetch_texel(0, wrap(x0 + 1, w, clamp_u), wrap(y0, h, clamp_v)), c10);
    unpack(fetch_texel(0, wrap(x0, w, clamp_u), wrap(y0 + 1, h, clamp_v)), c01);
    unpack(fetch_texel(0, wrap(x0 + 1, w, clamp_u), wrap(y0 + 1, h, clamp_v)), c11);
    for (int i = 0; i < 4; ++i) {
        const float top = c00[i] + (c10[i] - c00[i]) * tx;
        const float bottom = c01[i] + (c11[i] - c01[i]) * tx;
        out[i] = top + (bottom - top) * ty;
    }
}

} // namespace mhf::psp::gpu

namespace mhf::psp::gpu {

// ---------------------------------------------------------------- texture access for backends

Ge::TextureInfo Ge::texture_info() const {
    TextureInfo t;
    t.address = (regs_[kTexAddr0] & 0xFFFFF0u) | ((regs_[kTexBufWidth0] & 0x0F0000u) << 8u);
    t.stride = regs_[kTexBufWidth0] & 0x7FFu;
    t.format = regs_[kTexFormat] & 0xFu;
    t.width = 1u << (regs_[kTexSize0] & 0xFu);
    t.height = 1u << ((regs_[kTexSize0] >> 8u) & 0xFu);
    t.swizzled = (regs_[kTexMode] & 1u) != 0;
    static constexpr std::uint32_t bits_for[11] = {16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8};
    const std::uint32_t bits = bits_for[std::min<std::uint32_t>(t.format, 10u)];
    const std::uint32_t rows = t.swizzled ? (t.height + 7u) & ~7u : t.height;
    t.bytes = std::max(t.stride, t.width) * rows * bits / 8u;
    return t;
}

void Ge::decode_texture(std::uint32_t *out) const {
    const TextureInfo t = texture_info();
    if ((t.format == 4u || t.format == 5u) && t.bytes > 0u) {
        if (const std::uint8_t *src = host(t.address, t.bytes)) {
            std::uint32_t pal[256];
            const std::uint32_t pal_count = (t.format == 4u) ? 16u : 256u;
            for (std::uint32_t i = 0; i < pal_count; ++i) pal[i] = clut_lookup(i);
            const std::uint32_t bits = (t.format == 4u) ? 4u : 8u;
            const std::uint32_t row_bytes = t.stride * bits / 8u;
            const std::uint32_t blocks_per_row = std::max(1u, row_bytes / 16u);
            for (std::uint32_t y = 0; y < t.height; ++y) {
                std::uint32_t *dst_row = out + y * t.width;
                for (std::uint32_t x = 0; x < t.width; ++x) {
                    const std::uint32_t xbyte = x * bits / 8u;
                    const std::uint32_t offset =
                        t.swizzled ? (((y / 8u) * blocks_per_row + xbyte / 16u) * 128u + (y % 8u) * 16u + (xbyte % 16u))
                                   : (y * row_bytes + xbyte);
                    const std::uint8_t byte = src[offset];
                    dst_row[x] = (t.format == 4u) ? pal[(byte >> ((x & 1u) * 4u)) & 0xFu] : pal[byte];
                }
            }
            return;
        }
    }
    for (std::uint32_t y = 0; y < t.height; ++y) {
        for (std::uint32_t x = 0; x < t.width; ++x) {
            out[y * t.width + x] = fetch_texel(0, static_cast<int>(x), static_cast<int>(y));
        }
    }
}

std::uint64_t Ge::texture_hash() const {
    const TextureInfo t = texture_info();
    // FNV-1a over 64-bit words: fast and good enough to tell textures apart.
    std::uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 0x100000001B3ull;
    };
    mix(t.address);
    mix((static_cast<std::uint64_t>(t.format) << 32u) | (t.width << 16u) | t.height);
    mix(t.stride | (t.swizzled ? 0x10000u : 0u));
    if (const std::uint8_t *p = host(t.address, t.bytes)) {
        std::uint32_t i = 0;
        for (; i + 8u <= t.bytes; i += 8u) {
            std::uint64_t v;
            std::memcpy(&v, p + i, 8);
            mix(v);
        }
        for (; i < t.bytes; ++i) mix(p[i]);
    }
    if (t.format >= 4u && t.format <= 7u) {
        mix(regs_[kClutFormat]);
        for (const std::uint32_t c : clut_) mix(c);
    }
    return h;
}

void Ge::decode_hw_vertex(std::uint32_t address, const VertexLayout &l, HwVertex &out) const {
    const std::uint32_t vt = regs_[kVertexType];
    const unsigned tc = vt & 3u, color = (vt >> 2u) & 7u, normal = (vt >> 5u) & 3u, pos = (vt >> 7u) & 3u;
    const unsigned weight = (vt >> 9u) & 3u, weights = ((vt >> 14u) & 7u) + 1u;
    const unsigned pf = pos != 0u ? pos : 3u;

    auto to_u16_w = [](float w) -> std::uint16_t {
        const float scaled = w * 32768.0f;
        return scaled <= 0.0f ? 0u : (scaled >= 65535.0f ? 65535u : static_cast<std::uint16_t>(scaled + 0.5f));
    };
    auto to_s16_n = [](float n) -> std::int16_t {
        const float scaled = n * 32767.0f;
        return scaled <= -32767.0f ? -32767 : (scaled >= 32767.0f ? 32767 : static_cast<std::int16_t>(scaled));
    };

    if (const std::uint8_t *base = host(address, l.size)) {
        auto ps8 = [](const std::uint8_t *p) { return static_cast<float>(static_cast<std::int8_t>(*p)); };
        auto pu8 = [](const std::uint8_t *p) { return static_cast<float>(*p); };
        auto ps16 = [](const std::uint8_t *p) {
            std::int16_t v;
            std::memcpy(&v, p, 2);
            return static_cast<float>(v);
        };
        auto pu16 = [](const std::uint8_t *p) {
            std::uint16_t v;
            std::memcpy(&v, p, 2);
            return static_cast<float>(v);
        };
        auto pf32 = [](const std::uint8_t *p) {
            float v;
            std::memcpy(&v, p, 4);
            return v;
        };
        if (weight != 0u) {
            const std::uint8_t *wp = base + l.weight_offset;
            unsigned i = 0;
            for (; i < weights && i < 8u; ++i, wp += l.weight_step) {
                if (weight == 1u) out.weights[i] = static_cast<std::uint16_t>(wp[0]) << 8u;
                else if (weight == 2u) std::memcpy(&out.weights[i], wp, 2);
                else out.weights[i] = to_u16_w(pf32(wp));
            }
            for (; i < 8u; ++i) out.weights[i] = 0u;
        }
        if (tc != 0u) {
            const std::uint8_t *tp = base + l.tc_offset;
            for (unsigned i = 0; i < 2u; ++i, tp += l.tc_step) {
                const float t = (tc == 1u) ? pu8(tp) : (tc == 2u) ? pu16(tp) : pf32(tp);
                out.uv[i] = (tc == 1u) ? (t * (1.0f / 128.0f)) : (tc == 2u) ? (t * (1.0f / 32768.0f)) : t;
            }
        } else {
            out.uv[0] = out.uv[1] = 0.0f;
        }
        if (color != 0u) {
            const std::uint8_t *cp = base + l.color_offset;
            if (color == 7u) {
                std::memcpy(&out.color, cp, 4);
            } else {
                std::uint16_t raw;
                std::memcpy(&raw, cp, 2);
                out.color = decode16(raw, color - 4u);
            }
        } else {
            out.color = 0xFFFFFFFFu;
        }
        if (normal != 0u) {
            const std::uint8_t *np = base + l.normal_offset;
            for (unsigned i = 0; i < 3u; ++i, np += l.normal_step) {
                if (normal == 1u) out.normal[i] = static_cast<std::int16_t>(static_cast<std::int8_t>(*np)) << 8;
                else if (normal == 2u) std::memcpy(&out.normal[i], np, 2);
                else out.normal[i] = to_s16_n(pf32(np));
            }
        } else {
            out.normal[0] = out.normal[1] = out.normal[2] = 0;
        }
        const std::uint8_t *pp = base + l.pos_offset;
        for (unsigned i = 0; i < 3u; ++i, pp += l.pos_step) {
            out.pos[i] = (pf == 1u) ? (ps8(pp) * (1.0f / 128.0f))
                       : (pf == 2u) ? (ps16(pp) * (1.0f / 32768.0f))
                                    : pf32(pp);
        }
        return;
    }

    out = HwVertex{};
    auto s8 = [&](std::uint32_t a) { return static_cast<float>(static_cast<std::int8_t>(rd8(a))); };
    auto u8 = [&](std::uint32_t a) { return static_cast<float>(rd8(a)); };
    auto s16 = [&](std::uint32_t a) { return static_cast<float>(static_cast<std::int16_t>(rd16(a))); };
    auto u16 = [&](std::uint32_t a) { return static_cast<float>(rd16(a)); };
    auto f32 = [&](std::uint32_t a) { return std::bit_cast<float>(rd32(a)); };
    if (weight != 0u) {
        for (unsigned i = 0; i < weights && i < 8u; ++i) {
            const std::uint32_t a = address + l.weight_offset + i * l.weight_step;
            out.weights[i] = (weight == 1u) ? static_cast<std::uint16_t>(rd8(a) << 8u)
                           : (weight == 2u) ? rd16(a)
                                            : to_u16_w(f32(a));
        }
    }
    if (tc != 0u) {
        for (unsigned i = 0; i < 2u; ++i) {
            const std::uint32_t a = address + l.tc_offset + i * l.tc_step;
            const float t = (tc == 1u) ? u8(a) : (tc == 2u) ? u16(a) : f32(a);
            out.uv[i] = (tc == 1u) ? (t / 128.0f) : (tc == 2u) ? (t / 32768.0f) : t;
        }
    }
    if (color != 0u) {
        const std::uint32_t a = address + l.color_offset;
        out.color = (color == 7u) ? rd32(a) : decode16(rd16(a), color - 4u);
    }
    if (normal != 0u) {
        for (unsigned i = 0; i < 3u; ++i) {
            const std::uint32_t a = address + l.normal_offset + i * l.normal_step;
            out.normal[i] = (normal == 1u) ? static_cast<std::int16_t>(static_cast<std::int8_t>(rd8(a)) << 8)
                          : (normal == 2u) ? static_cast<std::int16_t>(rd16(a))
                                           : to_s16_n(f32(a));
        }
    }
    for (unsigned i = 0; i < 3u; ++i) {
        const std::uint32_t a = address + l.pos_offset + i * l.pos_step;
        out.pos[i] = (pf == 1u) ? (s8(a) / 128.0f) : (pf == 2u) ? (s16(a) / 32768.0f) : f32(a);
    }
}

void Ge::fill_hardware_state(HardwareDrawState &out, bool has_color_attr) const {
    auto &d = out.data;
    auto put_mat43 = [&](std::size_t vec4_idx, const Matrix43 &m) {
        const std::size_t base = vec4_idx * 4u;
        // Pack 4x3 column-major matrix into 3 vec4s:
        // vec0 = (m[0], m[3], m[6], m[9])  (row 0)
        // vec1 = (m[1], m[4], m[7], m[10]) (row 1)
        // vec2 = (m[2], m[5], m[8], m[11]) (row 2)
        d[base + 0] = m.m[0]; d[base + 1] = m.m[3]; d[base + 2]  = m.m[6]; d[base + 3]  = m.m[9];
        d[base + 4] = m.m[1]; d[base + 5] = m.m[4]; d[base + 6]  = m.m[7]; d[base + 7]  = m.m[10];
        d[base + 8] = m.m[2]; d[base + 9] = m.m[5]; d[base + 10] = m.m[8]; d[base + 11] = m.m[11];
    };

    const std::uint32_t vt = regs_[kVertexType];
    const unsigned skin_count = (((vt >> 9u) & 3u) != 0u) ? (((vt >> 14u) & 7u) + 1u) : 0u;
    for (std::size_t b = 0; b < skin_count && b < 8u; ++b) {
        put_mat43(b * 3u, bones_[b]);
    }
    put_mat43(24u, world_);
    put_mat43(27u, view_);
    std::copy(proj_.m.begin(), proj_.m.end(), d.begin() + 30u * 4u); // 30..33
    const std::uint32_t tex_mode = regs_[kTexMapMode] & 3u;
    if (tex_mode == 1u) put_mat43(34u, texgen_);

    // 37: tex_xform
    d[37 * 4 + 0] = float24(regs_[kTexScaleU]);
    d[37 * 4 + 1] = float24(regs_[kTexScaleV]);
    d[37 * 4 + 2] = float24(regs_[kTexOffsetU]);
    d[37 * 4 + 3] = float24(regs_[kTexOffsetV]);

    // 38: fog & texgen mode
    d[38 * 4 + 0] = float24(regs_[kFog1]);
    d[38 * 4 + 1] = float24(regs_[kFog2]);
    d[38 * 4 + 2] = static_cast<float>(regs_[kFogEnable] & 1u);
    d[38 * 4 + 3] = static_cast<float>(tex_mode | (((regs_[kTexMapMode] >> 8u) & 3u) << 2u));

    // 39..43: lighting materials & global ambient
    const bool lighting_on = (regs_[kLightingEnable] & 1u) != 0u;
    const LightSetup &ls = light_setup_;
    d[39 * 4 + 0] = ls.emissive[0]; d[39 * 4 + 1] = ls.emissive[1]; d[39 * 4 + 2] = ls.emissive[2];
    d[39 * 4 + 3] = lighting_on ? 1.0f : 0.0f;

    float amb[4]{};
    unpack(regs_[kMaterialAmbient] | (regs_[kMaterialAlpha] << 24u), amb);
    d[40 * 4 + 0] = amb[0]; d[40 * 4 + 1] = amb[1]; d[40 * 4 + 2] = amb[2]; d[40 * 4 + 3] = amb[3];

    if (lighting_on) {
        d[41 * 4 + 0] = ls.diffuse[0]; d[41 * 4 + 1] = ls.diffuse[1]; d[41 * 4 + 2] = ls.diffuse[2];
        d[41 * 4 + 3] = ls.shininess;

        d[42 * 4 + 0] = ls.specular[0]; d[42 * 4 + 1] = ls.specular[1]; d[42 * 4 + 2] = ls.specular[2];
        d[42 * 4 + 3] = static_cast<float>(has_color_attr ? ls.update : 0u);

        d[43 * 4 + 0] = ls.global[0]; d[43 * 4 + 1] = ls.global[1]; d[43 * 4 + 2] = ls.global[2]; d[43 * 4 + 3] = ls.global[3];
    }

    // 44: flags (skin_count, reverse_normal, separate_specular, has_color_attr)
    d[44 * 4 + 0] = static_cast<float>(skin_count);
    d[44 * 4 + 1] = static_cast<float>(ls.reverse_normal ? 1 : 0);
    d[44 * 4 + 2] = static_cast<float>(ls.separate ? 1 : 0);
    d[44 * 4 + 3] = static_cast<float>(has_color_attr ? 1 : 0);

    // 45..68: 4 lights (6 vec4 each)
    if (lighting_on) {
        for (std::size_t i = 0; i < 4u; ++i) {
            const auto &l = ls.lights[i];
            const std::size_t lb = (45u + i * 6u) * 4u;
            d[lb + 3] = l.on ? static_cast<float>(1u + l.kind) : 0.0f; // 0=off, 1=dir, 2=point, 3=spot
            if (!l.on) continue;
            d[lb + 0] = l.lp[0]; d[lb + 1] = l.lp[1]; d[lb + 2] = l.lp[2];
            d[lb + 4] = l.ld[0]; d[lb + 5] = l.ld[1]; d[lb + 6] = l.ld[2];
            d[lb + 7] = static_cast<float>(l.comp);
            d[lb + 8] = l.att[0]; d[lb + 9] = l.att[1]; d[lb + 10] = l.att[2];
            d[lb + 11] = l.cutoff;
            d[lb + 12] = l.lac[0]; d[lb + 13] = l.lac[1]; d[lb + 14] = l.lac[2];
            d[lb + 15] = l.spot_exp;
            d[lb + 16] = l.ldc[0]; d[lb + 17] = l.ldc[1]; d[lb + 18] = l.ldc[2];
            d[lb + 19] = 0.0f;
            d[lb + 20] = l.lsc[0]; d[lb + 21] = l.lsc[1]; d[lb + 22] = l.lsc[2];
            d[lb + 23] = 0.0f;
        }
    }

    // 69..70: viewport
    const float offset_x = static_cast<float>(regs_[kOffsetX] & 0xFFFFu) / 16.0f;
    const float offset_y = static_cast<float>(regs_[kOffsetY] & 0xFFFFu) / 16.0f;
    d[69 * 4 + 0] = float24(regs_[kViewportXScale]);
    d[69 * 4 + 1] = float24(regs_[kViewportYScale]);
    d[69 * 4 + 2] = float24(regs_[kViewportXCenter]) - offset_x;
    d[69 * 4 + 3] = float24(regs_[kViewportYCenter]) - offset_y;
    d[70 * 4 + 0] = float24(regs_[kViewportZScale]);
    d[70 * 4 + 1] = float24(regs_[kViewportZCenter]);
    d[70 * 4 + 2] = offset_x;
    d[70 * 4 + 3] = offset_y;
}

} // namespace mhf::psp::gpu
