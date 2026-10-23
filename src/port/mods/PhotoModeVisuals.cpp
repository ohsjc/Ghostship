#include "PhotoModeVisuals.h"
#include "PhotoMode.h"
#include "PhotoModeFilters.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

#include "port/ui/cvar_prefixes.h"

#include "sm64.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "game/area.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/memory.h"
#include "game/rendering_graph_node.h"
#include "port/interpolation/FrameInterpolation.h"

// math_util.h defines min/max macros that break std::min/std::max.
#undef min
#undef max

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#include "../../../Torch/lib/n64graphics/stb_image.h"

// skybox.h has no C linkage markers.
extern "C" Gfx* create_skybox_facing_camera(s8 player, s8 background, f32 fov, f32 posX, f32 posY, f32 posZ, f32 focX,
                                            f32 focY, f32 focZ);

#define PHOTO_CVAR(name) CVAR_ENHANCEMENT("PhotoMode." name)

namespace fs = std::filesystem;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kSkyRadius = 1000.0f;

// Custom images are cut into 256x256 RGBA32 textures. Each holds 254x254 image pixels plus a
// 1-pixel border copied from the neighbours, so bilinear filtering has no seams between tiles.
constexpr int kTileSize = 256;
constexpr int kTileContent = kTileSize - 2;
constexpr int kMaxImageWidth = 8192;
constexpr int kMaxImageHeight = 4096;

struct SkyTile {
    std::unique_ptr<u8[]> pixels; // kTileSize * kTileSize * 4, RGBA
    int srcX, srcY, width, height; // image pixels covered (excluding the border)
};

struct SkyImage {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<SkyTile> tiles;
    int tilesX = 0;
    int tilesY = 0;
};

SkyImage sImage;
std::string sLoadedName;
std::string sStatus;
bool sReloadRequested = false;
bool sInvalidatePending = false;

// Static geometry, rebuilt only when the settings that shape it change.
std::vector<Vtx> sBaseVtx;
std::vector<Gfx> sBaseDl;
std::vector<Vtx> sImageVtx;
std::vector<Gfx> sImageDl;
std::string sBaseKey;
std::string sImageKey;

float sSpin = 0.0f; // degrees, accumulated while spinning

int GetColor(const char* name, int def) {
    return CVarGetInteger(name, def) & 0xFFFFFF;
}

void SetVertex(Vtx* v, float lon, float lat, int s, int t, int color) {
    float cl = cosf(lat);
    v->v.ob[0] = (s16)lroundf(sinf(lon) * cl * kSkyRadius);
    v->v.ob[1] = (s16)lroundf(sinf(lat) * kSkyRadius);
    v->v.ob[2] = (s16)lroundf(cosf(lon) * cl * kSkyRadius);
    v->v.flag = 0;
    v->v.tc[0] = (s16)s;
    v->v.tc[1] = (s16)t;
    v->v.cn[0] = (color >> 16) & 0xFF;
    v->v.cn[1] = (color >> 8) & 0xFF;
    v->v.cn[2] = color & 0xFF;
    v->v.cn[3] = 0xFF;
}

int LerpColor(int a, int b, float t) {
    int out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        float ca = (float)((a >> shift) & 0xFF);
        float cb = (float)((b >> shift) & 0xFF);
        out |= ((int)lroundf(ca + (cb - ca) * t) & 0xFF) << shift;
    }
    return out;
}

int Segments(float angle) {
    return std::clamp((int)ceilf(fabsf(angle) / (8.0f * kDegToRad)), 1, 15);
}

// A grid patch of the sphere. Vertices are generated per pair of rows so each load stays well
// within the 32-vertex buffer. vtx must have enough reserved capacity (no reallocation).
struct PatchSpec {
    float lon0, lon1, lat0, lat1; // lat0 = top
    float s0, s1, t0, t1;         // texel coordinates
    int colorTop, colorBottom;
};

size_t PatchVertexCount(const PatchSpec& p) {
    int nx = Segments(p.lon1 - p.lon0);
    int ny = Segments(p.lat1 - p.lat0);
    return (size_t)ny * 2 * (nx + 1);
}

void EmitPatch(const PatchSpec& p, std::vector<Vtx>& vtx, std::vector<Gfx>& dl) {
    int nx = Segments(p.lon1 - p.lon0);
    int ny = Segments(p.lat1 - p.lat0);
    for (int j = 0; j < ny; j++) {
        size_t start = vtx.size();
        for (int row = 0; row < 2; row++) {
            float fy = (float)(j + row) / ny;
            float lat = p.lat0 + (p.lat1 - p.lat0) * fy;
            int t = (int)lroundf((p.t0 + (p.t1 - p.t0) * fy) * 32.0f);
            int color = LerpColor(p.colorTop, p.colorBottom, fy);
            for (int i = 0; i <= nx; i++) {
                float fx = (float)i / nx;
                float lon = p.lon0 + (p.lon1 - p.lon0) * fx;
                int s = (int)lroundf((p.s0 + (p.s1 - p.s0) * fx) * 32.0f);
                vtx.emplace_back();
                SetVertex(&vtx.back(), lon, lat, s, t, color);
            }
        }
        Gfx g;
        gSPVertex(&g, (uintptr_t)&vtx[start], 2 * (nx + 1), 0);
        dl.push_back(g);
        for (int i = 0; i < nx; i++) {
            int a = i, b = i + 1, c = nx + 1 + i, d = nx + 2 + i;
            gSP2Triangles(&g, a, c, b, 0, b, c, d, 0);
            dl.push_back(g);
        }
    }
}

// Untextured sphere: one color (solid) or a vertical gradient.
void BuildBaseSphere(int top, int bottom) {
    std::string key = std::to_string(top) + "/" + std::to_string(bottom);
    if (key == sBaseKey && !sBaseDl.empty()) {
        return;
    }
    sBaseKey = key;
    std::vector<PatchSpec> patches;
    constexpr int kLon = 8;
    constexpr int kLat = 4;
    for (int x = 0; x < kLon; x++) {
        for (int y = 0; y < kLat; y++) {
            float fy0 = (float)y / kLat, fy1 = (float)(y + 1) / kLat;
            PatchSpec p;
            p.lon0 = 2.0f * kPi * x / kLon;
            p.lon1 = 2.0f * kPi * (x + 1) / kLon;
            p.lat0 = kPi / 2 - kPi * fy0;
            p.lat1 = kPi / 2 - kPi * fy1;
            p.s0 = p.s1 = p.t0 = p.t1 = 0.0f;
            p.colorTop = LerpColor(top, bottom, fy0);
            p.colorBottom = LerpColor(top, bottom, fy1);
            patches.push_back(p);
        }
    }
    size_t count = 0;
    for (auto& p : patches) {
        count += PatchVertexCount(p);
    }
    sBaseVtx.clear();
    sBaseVtx.reserve(count);
    sBaseDl.clear();
    for (auto& p : patches) {
        EmitPatch(p, sBaseVtx, sBaseDl);
    }
    Gfx g;
    gSPEndDisplayList(&g);
    sBaseDl.push_back(g);
}

const u8* PixelAt(const u8* data, int w, int h, int x, int y) {
    x = ((x % w) + w) % w; // wraps around the 360 degrees
    y = std::clamp(y, 0, h - 1);
    return data + ((size_t)y * w + x) * 4;
}

bool LoadImage(const std::string& name) {
    sImage = SkyImage();
    sImageKey.clear();
    fs::path path = fs::path(PhotoModeVisuals_GetSkyboxDirectory()) / name;

    // Read through std::ifstream so non-ASCII Windows paths work.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        sStatus = "Could not open " + path.string();
        return false;
    }
    std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, channels = 0;
    u8* data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &channels, 4);
    if (data == nullptr) {
        sStatus = "Could not read " + name + ": " + stbi_failure_reason();
        return false;
    }

    // Shrink very large images by an integer factor (box filter).
    int factor = 1;
    while (w / factor > kMaxImageWidth || h / factor > kMaxImageHeight) {
        factor++;
    }
    std::vector<u8> scaled;
    const u8* src = data;
    int origW = w, origH = h;
    if (factor > 1) {
        int nw = w / factor, nh = h / factor;
        scaled.resize((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++) {
            for (int x = 0; x < nw; x++) {
                for (int c = 0; c < 4; c++) {
                    int sum = 0;
                    for (int dy = 0; dy < factor; dy++) {
                        for (int dx = 0; dx < factor; dx++) {
                            sum += data[((size_t)(y * factor + dy) * w + (x * factor + dx)) * 4 + c];
                        }
                    }
                    scaled[((size_t)y * nw + x) * 4 + c] = (u8)(sum / (factor * factor));
                }
            }
        }
        w = nw;
        h = nh;
        src = scaled.data();
    }

    sImage.name = name;
    sImage.width = w;
    sImage.height = h;
    sImage.tilesX = (w + kTileContent - 1) / kTileContent;
    sImage.tilesY = (h + kTileContent - 1) / kTileContent;
    for (int ty = 0; ty < sImage.tilesY; ty++) {
        for (int tx = 0; tx < sImage.tilesX; tx++) {
            SkyTile tile;
            tile.srcX = tx * kTileContent;
            tile.srcY = ty * kTileContent;
            tile.width = std::min(kTileContent, w - tile.srcX);
            tile.height = std::min(kTileContent, h - tile.srcY);
            tile.pixels = std::make_unique<u8[]>(kTileSize * kTileSize * 4);
            for (int y = 0; y < kTileSize; y++) {
                // Rows past the content repeat the last row (and the bottom border).
                int sy = tile.srcY + std::min(y, tile.height + 1) - 1;
                for (int x = 0; x < kTileSize; x++) {
                    int sx = tile.srcX + std::min(x, tile.width + 1) - 1;
                    memcpy(&tile.pixels[((size_t)y * kTileSize + x) * 4], PixelAt(src, w, h, sx, sy), 4);
                }
            }
            sImage.tiles.push_back(std::move(tile));
        }
    }
    stbi_image_free(data);

    sStatus = "Loaded " + name + " (" + std::to_string(origW) + "x" + std::to_string(origH);
    if (factor > 1) {
        sStatus += ", reduced to " + std::to_string(w) + "x" + std::to_string(h);
    }
    sStatus += ")";
    // Tile buffers may reuse addresses of old ones; drop any cached textures for them.
    sInvalidatePending = true;
    return true;
}

void BuildImageSphere() {
    int repeats = std::clamp(CVarGetInteger(PHOTO_CVAR("Sky.Repeats"), 1), 1, 16);
    bool fill = CVarGetInteger(PHOTO_CVAR("Sky.FillSphere"), 1) != 0;
    bool mirror = CVarGetInteger(PHOTO_CVAR("Sky.Mirror"), 0) != 0;
    float horizon = CVarGetFloat(PHOTO_CVAR("Sky.Horizon"), 0.0f) * kDegToRad;
    float vScale = std::clamp(CVarGetFloat(PHOTO_CVAR("Sky.VScale"), 1.0f), 0.05f, 10.0f);

    char keyBuf[160];
    snprintf(keyBuf, sizeof(keyBuf), "%s|%d|%d|%d|%d|%.3f|%.3f|%d", sImage.name.c_str(), sImage.width, repeats, fill,
             mirror, horizon, vScale, (int)sImage.tiles.size());
    if (sImageKey == keyBuf && !sImageDl.empty()) {
        return;
    }
    sImageKey = keyBuf;

    float span = 2.0f * kPi / repeats;
    float vSpan = fill ? kPi : span * ((float)sImage.height / sImage.width) * vScale;
    float latTop = fill ? kPi / 2 : horizon + vSpan / 2;
    auto latAt = [&](float v) { return std::clamp(latTop - v * vSpan, -kPi / 2, kPi / 2); };

    struct TilePatch {
        const SkyTile* tile;
        PatchSpec spec;
    };
    std::vector<TilePatch> patches;
    for (int k = 0; k < repeats; k++) {
        for (const auto& tile : sImage.tiles) {
            float u0 = (float)tile.srcX / sImage.width;
            float u1 = (float)(tile.srcX + tile.width) / sImage.width;
            if (mirror) {
                u0 = 1.0f - u0;
                u1 = 1.0f - u1;
            }
            TilePatch tp;
            tp.tile = &tile;
            // Looking along +Z, the right side of the screen is -X, so the image runs toward lower angles.
            tp.spec.lon0 = -(k + u0) * span;
            tp.spec.lon1 = -(k + u1) * span;
            tp.spec.lat0 = latAt((float)tile.srcY / sImage.height);
            tp.spec.lat1 = latAt((float)(tile.srcY + tile.height) / sImage.height);
            tp.spec.s0 = 1.0f;
            tp.spec.s1 = 1.0f + tile.width;
            tp.spec.t0 = 1.0f;
            tp.spec.t1 = 1.0f + tile.height;
            tp.spec.colorTop = tp.spec.colorBottom = 0xFFFFFF;
            patches.push_back(tp);
        }
    }

    size_t count = 0;
    for (auto& tp : patches) {
        count += PatchVertexCount(tp.spec);
    }
    sImageVtx.clear();
    sImageVtx.reserve(count);
    sImageDl.clear();
    Gfx g;
    for (auto& tp : patches) {
        const u8* tex = tp.tile->pixels.get();
        gDPSetTextureImage(&g, G_IM_FMT_RGBA, G_IM_SIZ_32b, kTileSize, tex);
        sImageDl.push_back(g);
        gDPSetTile(&g, G_IM_FMT_RGBA, G_IM_SIZ_32b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, 0, 0,
                   G_TX_NOMIRROR | G_TX_CLAMP, 0, 0);
        sImageDl.push_back(g);
        gDPLoadSync(&g);
        sImageDl.push_back(g);
        gDPLoadTile(&g, G_TX_LOADTILE, 0, 0, (kTileSize - 1) << G_TEXTURE_IMAGE_FRAC,
                    (kTileSize - 1) << G_TEXTURE_IMAGE_FRAC);
        sImageDl.push_back(g);
        gDPPipeSync(&g);
        sImageDl.push_back(g);
        gDPSetTile(&g, G_IM_FMT_RGBA, G_IM_SIZ_32b, (kTileSize * 4) >> 3, 0, G_TX_RENDERTILE, 0,
                   G_TX_NOMIRROR | G_TX_CLAMP, 0, 0, G_TX_NOMIRROR | G_TX_CLAMP, 0, 0);
        sImageDl.push_back(g);
        gDPSetTileSize(&g, G_TX_RENDERTILE, 0, 0, (kTileSize - 1) << G_TEXTURE_IMAGE_FRAC,
                       (kTileSize - 1) << G_TEXTURE_IMAGE_FRAC);
        sImageDl.push_back(g);
        EmitPatch(tp.spec, sImageVtx, sImageDl);
    }
    gSPEndDisplayList(&g);
    sImageDl.push_back(g);
}

void UpdateImage() {
    std::string name = CVarGetString(PHOTO_CVAR("Sky.Image"), "");
    if (name != sLoadedName || sReloadRequested) {
        sReloadRequested = false;
        sLoadedName = name;
        if (name.empty()) {
            sImage = SkyImage();
            sImageKey.clear();
            sStatus = "No image selected.";
        } else {
            LoadImage(name);
        }
    }
}

Gfx* BuildSkyDisplayList(PhotoModeSkyMode mode) {
    bool image = false;
    if (mode == PhotoModeSkyMode::Solid) {
        int color = GetColor(PHOTO_CVAR("Sky.Color"), 0x87CEEB);
        BuildBaseSphere(color, color);
    } else if (mode == PhotoModeSkyMode::Gradient) {
        BuildBaseSphere(GetColor(PHOTO_CVAR("Sky.TopColor"), 0x1E3C78), GetColor(PHOTO_CVAR("Sky.BottomColor"), 0xF0A060));
    } else {
        UpdateImage();
        int fillColor = GetColor(PHOTO_CVAR("Sky.FillColor"), 0x000000);
        BuildBaseSphere(fillColor, fillColor);
        image = !sImage.tiles.empty();
        if (image) {
            BuildImageSphere();
        }
    }

    // Same projection as the level camera: perspective * stretch * screen roll, then a view
    // rotation without translation so the sphere stays centered on the camera.
    float fov = CVarGetFloat(PHOTO_CVAR("Fov"), 45.0f);
    float aspect = gCurGraphNodeRoot != nullptr && gCurGraphNodeRoot->height != 0
                       ? (float)gCurGraphNodeRoot->width / (float)gCurGraphNodeRoot->height
                       : 4.0f / 3.0f;

    Mtx* persp = (Mtx*)alloc_display_list(sizeof(Mtx));
    Mtx* stretch = (Mtx*)alloc_display_list(sizeof(Mtx));
    Mtx* roll = (Mtx*)alloc_display_list(sizeof(Mtx));
    Mtx* view = (Mtx*)alloc_display_list(sizeof(Mtx));
    size_t cmdCount = 40 + (sInvalidatePending ? sImage.tiles.size() : 0);
    Gfx* start = (Gfx*)alloc_display_list((u32)(cmdCount * sizeof(Gfx)));
    if (persp == nullptr || stretch == nullptr || roll == nullptr || view == nullptr || start == nullptr) {
        return nullptr;
    }

    u16 perspNorm;
    guPerspective(persp, &perspNorm, fov, aspect, 10.0f, kSkyRadius * 4.0f, 1.0f);
    float sx, sy;
    PhotoModeVisuals_GetStretch(&sx, &sy);
    guScale(stretch, sx, sy, 1.0f);

    float spinSpeed = CVarGetFloat(PHOTO_CVAR("Sky.Spin"), 0.0f);
    if (mode == PhotoModeSkyMode::CustomImage && spinSpeed != 0.0f) {
        sSpin = fmodf(sSpin + spinSpeed / 30.0f, 360.0f);
    }
    float rotation = (mode == PhotoModeSkyMode::CustomImage ? CVarGetFloat(PHOTO_CVAR("Sky.Rotation"), 0.0f) + sSpin
                                                            : 0.0f) *
                     kDegToRad;

    // Rotating the look direction backwards turns the sky forwards.
    float dx = gLakituState.focus[0] - gLakituState.pos[0];
    float dy = gLakituState.focus[1] - gLakituState.pos[1];
    float dz = gLakituState.focus[2] - gLakituState.pos[2];
    float c = cosf(-rotation), s = sinf(-rotation);
    Vec3f origin = { 0.0f, 0.0f, 0.0f };
    Vec3f dir = { dx * c + dz * s, dy, -dx * s + dz * c };
    Mat4 viewF;
    FrameInterpolation_RecordOpenChild("photomode_sky", 0);
    mtxf_rotate_xy(roll, gLakituState.roll);
    mtxf_lookat(viewF, origin, dir, 0);
    mtxf_to_mtx(view, viewF);
    FrameInterpolation_RecordCloseChild();

    Gfx* g = start;
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_BOTH | G_FOG | G_ZBUFFER | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetRenderMode(g++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gSPPerspNormalize(g++, perspNorm);
    gSPMatrix(g++, VIRTUAL_TO_PHYSICAL(persp), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, VIRTUAL_TO_PHYSICAL(stretch), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(g++, VIRTUAL_TO_PHYSICAL(roll), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(g++, VIRTUAL_TO_PHYSICAL(view), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(g++, sBaseDl.data());

    if (image) {
        if (sInvalidatePending) {
            for (const auto& tile : sImage.tiles) {
                __gSPInvalidateTexCache(g++, (uintptr_t)tile.pixels.get());
            }
            sInvalidatePending = false;
        }
        int tint = GetColor(PHOTO_CVAR("Sky.Tint"), 0xFFFFFF);
        float brightness = std::clamp(CVarGetFloat(PHOTO_CVAR("Sky.Brightness"), 1.0f), 0.0f, 1.0f);
        gDPPipeSync(g++);
        gDPSetTextureFilter(g++, G_TF_BILERP);
        gDPSetTextureLUT(g++, G_TT_NONE);
        gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombineMode(g++, G_CC_MODULATEI_PRIM, G_CC_MODULATEI_PRIM);
        gDPSetPrimColor(g++, 0, 0, (u8)(((tint >> 16) & 0xFF) * brightness), (u8)(((tint >> 8) & 0xFF) * brightness),
                        (u8)((tint & 0xFF) * brightness), 255);
        gSPDisplayList(g++, sImageDl.data());
    }

    gDPPipeSync(g++);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPSetGeometryMode(g++, G_ZBUFFER);
    gSPEndDisplayList(g++);
    return start;
}

} // namespace

std::string PhotoModeVisuals_GetSkyboxDirectory() {
    return Ship::Context::GetPathRelativeToAppDirectory("photomode_skyboxes");
}

std::vector<std::string> PhotoModeVisuals_ListSkyboxImages() {
    std::vector<std::string> names;
    std::error_code ec;
    fs::path dir = PhotoModeVisuals_GetSkyboxDirectory();
    fs::create_directories(dir, ec);
    fs::path readme = dir / "README.txt";
    if (!fs::exists(readme, ec)) {
        std::ofstream out(readme);
        out << "Custom Photo Mode skyboxes\n"
               "==========================\n\n"
               "Drop .png, .jpg, .bmp or .tga images in this folder, then press Refresh in\n"
               "Photo Mode > Visual FX and pick the image.\n\n"
               "Best results: a 360-degree equirectangular panorama (2:1, e.g. 4096x2048).\n"
               "Ordinary pictures work too: turn off 'Fill Whole Sky', then use 'Repeats Around',\n"
               "'Horizon Height' and 'Vertical Scale' to place them. Images larger than 8192x4096\n"
               "are reduced automatically. Press 'Reload Image' after editing a file.\n";
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)tolower(ch); });
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void PhotoModeVisuals_ReloadSkyboxImage() {
    sReloadRequested = true;
}

const std::string& PhotoModeVisuals_GetSkyboxStatus() {
    return sStatus;
}

void PhotoModeVisuals_GetStretch(float* x, float* y) {
    *x = 1.0f;
    *y = 1.0f;
    if (PhotoMode_IsActive()) {
        *x = std::clamp(CVarGetFloat(PHOTO_CVAR("Stretch.X"), 1.0f), 0.05f, 20.0f);
        *y = std::clamp(CVarGetFloat(PHOTO_CVAR("Stretch.Y"), 1.0f), 0.05f, 20.0f);
    }
}

extern "C" bool PhotoMode_RenderSkybox(void** gfx) {
    if (!PhotoMode_IsActive()) {
        return false;
    }
    auto mode = (PhotoModeSkyMode)CVarGetInteger(PHOTO_CVAR("Sky.Mode"), (int)PhotoModeSkyMode::Default);
    switch (mode) {
        case PhotoModeSkyMode::Default:
            return false;
        case PhotoModeSkyMode::BuiltIn: {
            s8 bg = (s8)std::clamp(CVarGetInteger(PHOTO_CVAR("Sky.BuiltIn"), 0), 0, 9);
            *gfx = create_skybox_facing_camera(0, bg, 90.0f, gLakituState.pos[0], gLakituState.pos[1],
                                               gLakituState.pos[2], gLakituState.focus[0], gLakituState.focus[1],
                                               gLakituState.focus[2]);
            return *gfx != nullptr;
        }
        default:
            *gfx = BuildSkyDisplayList(mode);
            return *gfx != nullptr;
    }
}

extern "C" void PhotoMode_ApplyProjectionStretch(void) {
    float x, y;
    PhotoModeVisuals_GetStretch(&x, &y);
    if (x == 1.0f && y == 1.0f) {
        return;
    }
    Mtx* mtx = (Mtx*)alloc_display_list(sizeof(Mtx));
    if (mtx == nullptr) {
        return;
    }
    guScale(mtx, x, y, 1.0f);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
}

extern "C" bool PhotoMode_ScaleObject(struct Object* obj, f32 scale[3]) {
    if (!PhotoMode_IsActive()) {
        return false;
    }
    float all = CVarGetFloat(PHOTO_CVAR("Scale.All"), 1.0f);
    float axis[3] = { CVarGetFloat(PHOTO_CVAR("Scale.X"), 1.0f), CVarGetFloat(PHOTO_CVAR("Scale.Y"), 1.0f),
                      CVarGetFloat(PHOTO_CVAR("Scale.Z"), 1.0f) };
    float cat = PhotoModeFilters_GetScale(obj);
    float m = all * cat;
    if (m == 1.0f && axis[0] == 1.0f && axis[1] == 1.0f && axis[2] == 1.0f) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        scale[i] *= m * axis[i];
    }
    return true;
}
