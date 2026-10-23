#include "PhotoModeTracks.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>
#include <ship/Context.h>

#include "port/ui/cvar_prefixes.h"

#include "game/area.h"

#define TRACK_CVAR(name) CVAR_ENHANCEMENT("PhotoMode.Tracks." name)

namespace fs = std::filesystem;

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kFrameRate = 30.0f;

std::vector<PhotoModeKeyframe> sKeys;
int sSelected = -1;

bool sPlaying = false;
float sCountdown = 0.0f;
float sTime = 0.0f;

float Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float SmoothStep(float u) {
    return u * u * (3.0f - 2.0f * u);
}

float CatmullRom(float p0, float p1, float p2, float p3, float u) {
    float u2 = u * u;
    float u3 = u2 * u;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
}

float SegmentDuration(size_t i) {
    return std::max(sKeys[i].duration, 0.05f);
}

// Angles unwrapped so each keyframe takes the shortest turn from the previous one.
void UnwrapAngles(std::vector<float>& yaw, std::vector<float>& roll) {
    yaw.resize(sKeys.size());
    roll.resize(sKeys.size());
    for (size_t i = 0; i < sKeys.size(); i++) {
        yaw[i] = sKeys[i].pose.yaw;
        roll[i] = sKeys[i].pose.roll;
        if (i > 0) {
            yaw[i] = yaw[i - 1] + remainderf(yaw[i] - yaw[i - 1], kTwoPi);
            roll[i] = roll[i - 1] + remainderf(roll[i] - roll[i - 1], kTwoPi);
        }
    }
}

PhotoModePose EvaluateSegment(size_t seg, float u, const std::vector<float>& yaw, const std::vector<float>& roll) {
    size_t n = sKeys.size();
    size_t i0 = seg == 0 ? 0 : seg - 1;
    size_t i1 = seg;
    size_t i2 = std::min(seg + 1, n - 1);
    size_t i3 = std::min(seg + 2, n - 1);

    auto spline = [&](auto get) { return CatmullRom(get(i0), get(i1), get(i2), get(i3), u); };

    PhotoModePose p;
    for (int axis = 0; axis < 3; axis++) {
        p.pos[axis] = spline([&](size_t i) { return sKeys[i].pose.pos[axis]; });
    }
    p.yaw = spline([&](size_t i) { return yaw[i]; });
    p.pitch = spline([&](size_t i) { return sKeys[i].pose.pitch; });
    p.roll = spline([&](size_t i) { return roll[i]; });
    p.fov = spline([&](size_t i) { return sKeys[i].pose.fov; });
    return p;
}

std::string SanitizeName(const std::string& name) {
    std::string out;
    for (char ch : name) {
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == ' ') {
            out += ch;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

} // namespace

std::vector<PhotoModeKeyframe>& PhotoModeTracks_Keyframes() {
    return sKeys;
}

int PhotoModeTracks_GetSelected() {
    return sSelected;
}

void PhotoModeTracks_SetSelected(int index) {
    sSelected = sKeys.empty() ? -1 : std::clamp(index, 0, (int)sKeys.size() - 1);
}

void PhotoModeTracks_AddKeyframe(const PhotoModePose& pose) {
    PhotoModeKeyframe key;
    key.pose = pose;
    key.duration = CVarGetFloat(TRACK_CVAR("DefaultDuration"), 3.0f);
    int at = (sSelected >= 0 && sSelected < (int)sKeys.size()) ? sSelected + 1 : (int)sKeys.size();
    sKeys.insert(sKeys.begin() + at, key);
    sSelected = at;
}

void PhotoModeTracks_DeleteKeyframe(int index) {
    if (index < 0 || index >= (int)sKeys.size()) {
        return;
    }
    PhotoModeTracks_Stop();
    sKeys.erase(sKeys.begin() + index);
    if (sSelected >= (int)sKeys.size() || sSelected > index) {
        sSelected--;
    }
    if (sKeys.empty()) {
        sSelected = -1;
    }
}

void PhotoModeTracks_MoveKeyframe(int index, int direction) {
    int other = index + direction;
    if (index < 0 || other < 0 || index >= (int)sKeys.size() || other >= (int)sKeys.size()) {
        return;
    }
    std::swap(sKeys[index], sKeys[other]);
    if (sSelected == index) {
        sSelected = other;
    } else if (sSelected == other) {
        sSelected = index;
    }
}

void PhotoModeTracks_Clear() {
    PhotoModeTracks_Stop();
    sKeys.clear();
    sSelected = -1;
}

void PhotoModeTracks_JumpTo(int index) {
    if (index < 0 || index >= (int)sKeys.size()) {
        return;
    }
    PhotoModeTracks_Stop();
    sSelected = index;
    PhotoMode_SetPose(sKeys[index].pose);
}

bool PhotoModeTracks_CanPlay() {
    return sKeys.size() >= 2 && PhotoMode_IsActive();
}

void PhotoModeTracks_Play() {
    if (!PhotoModeTracks_CanPlay()) {
        return;
    }
    sPlaying = true;
    sTime = 0.0f;
    sCountdown = std::max(CVarGetFloat(TRACK_CVAR("Countdown"), 3.0f), 0.0f);
    PhotoMode_SetPose(sKeys[0].pose);
}

void PhotoModeTracks_Stop() {
    sPlaying = false;
    sCountdown = 0.0f;
}

void PhotoModeTracks_TogglePlay() {
    if (sPlaying) {
        PhotoModeTracks_Stop();
    } else {
        PhotoModeTracks_Play();
    }
}

bool PhotoModeTracks_IsPlaying() {
    return sPlaying;
}

float PhotoModeTracks_CountdownRemaining() {
    return sPlaying ? sCountdown : 0.0f;
}

float PhotoModeTracks_GetTime() {
    return sTime;
}

float PhotoModeTracks_GetDuration() {
    float total = 0.0f;
    for (size_t i = 0; i + 1 < sKeys.size(); i++) {
        total += SegmentDuration(i);
    }
    return total;
}

bool PhotoModeTracks_Advance(PhotoModePose* out) {
    if (!sPlaying) {
        return false;
    }
    if (sKeys.size() < 2) {
        PhotoModeTracks_Stop();
        return false;
    }

    if (sCountdown > 0.0f) {
        sCountdown -= 1.0f / kFrameRate;
        if (sCountdown < 0.0f) {
            sCountdown = 0.0f;
        }
        *out = PhotoModeTracks_Evaluate(0.0f);
        return true;
    }

    float duration = PhotoModeTracks_GetDuration();
    *out = PhotoModeTracks_Evaluate(sTime);

    float speed = Clamp(CVarGetFloat(TRACK_CVAR("Speed"), 1.0f), 0.05f, 10.0f);
    sTime += speed / kFrameRate;
    if (sTime > duration) {
        if (CVarGetInteger(TRACK_CVAR("Loop"), 0)) {
            sTime = fmodf(sTime, duration);
        } else {
            // Hold on the last frame; the next Advance call reports not playing.
            *out = PhotoModeTracks_Evaluate(duration);
            PhotoModeTracks_Stop();
        }
    }
    return true;
}

PhotoModePose PhotoModeTracks_Evaluate(float time) {
    if (sKeys.empty()) {
        return PhotoMode_GetPose();
    }
    if (sKeys.size() == 1) {
        return sKeys[0].pose;
    }

    float duration = PhotoModeTracks_GetDuration();
    float t = Clamp(time, 0.0f, duration);
    auto easing = (PhotoModeTrackEasing)CVarGetInteger(TRACK_CVAR("Easing"), (int)PhotoModeTrackEasing::EaseStartEnd);
    if (easing == PhotoModeTrackEasing::EaseStartEnd && duration > 0.0f) {
        t = SmoothStep(t / duration) * duration;
    }

    size_t seg = 0;
    while (seg + 2 < sKeys.size() && t > SegmentDuration(seg)) {
        t -= SegmentDuration(seg);
        seg++;
    }
    float u = Clamp(t / SegmentDuration(seg), 0.0f, 1.0f);
    if (easing == PhotoModeTrackEasing::EaseEachKeyframe) {
        u = SmoothStep(u);
    }

    std::vector<float> yaw, roll;
    UnwrapAngles(yaw, roll);
    return EvaluateSegment(seg, u, yaw, roll);
}

std::vector<PhotoModePose> PhotoModeTracks_SamplePath(int samplesPerSegment) {
    std::vector<PhotoModePose> out;
    if (sKeys.size() < 2) {
        return out;
    }
    std::vector<float> yaw, roll;
    UnwrapAngles(yaw, roll);
    for (size_t seg = 0; seg + 1 < sKeys.size(); seg++) {
        for (int s = 0; s < samplesPerSegment; s++) {
            out.push_back(EvaluateSegment(seg, (float)s / samplesPerSegment, yaw, roll));
        }
    }
    out.push_back(sKeys.back().pose);
    return out;
}

std::string PhotoModeTracks_GetDirectory() {
    return Ship::Context::GetPathRelativeToAppDirectory("photomode_tracks");
}

bool PhotoModeTracks_Save(const std::string& rawName, std::string* message) {
    std::string name = SanitizeName(rawName);
    if (name.empty()) {
        *message = "Enter a name (letters, numbers, - _ and spaces).";
        return false;
    }
    if (sKeys.empty()) {
        *message = "Nothing to save: add keyframes first.";
        return false;
    }

    nlohmann::json j;
    j["version"] = 1;
    j["level"] = gCurrLevelNum;
    j["area"] = gCurrAreaIndex;
    j["keyframes"] = nlohmann::json::array();
    for (const auto& k : sKeys) {
        j["keyframes"].push_back({
            { "pos", { k.pose.pos[0], k.pose.pos[1], k.pose.pos[2] } },
            { "yaw", k.pose.yaw },
            { "pitch", k.pose.pitch },
            { "roll", k.pose.roll },
            { "fov", k.pose.fov },
            { "duration", k.duration },
        });
    }

    try {
        fs::create_directories(PhotoModeTracks_GetDirectory());
        fs::path path = fs::path(PhotoModeTracks_GetDirectory()) / (name + ".json");
        std::ofstream file(path);
        if (!file) {
            *message = "Could not write " + path.string();
            return false;
        }
        file << j.dump(2);
        *message = "Saved " + name + " (" + std::to_string(sKeys.size()) + " keyframes).";
        return true;
    } catch (const std::exception& e) {
        *message = std::string("Save failed: ") + e.what();
        return false;
    }
}

bool PhotoModeTracks_Load(const std::string& rawName, std::string* message) {
    std::string name = SanitizeName(rawName);
    fs::path path = fs::path(PhotoModeTracks_GetDirectory()) / (name + ".json");
    try {
        std::ifstream file(path);
        if (!file) {
            *message = "Could not open " + path.string();
            return false;
        }
        nlohmann::json j = nlohmann::json::parse(file);

        std::vector<PhotoModeKeyframe> keys;
        for (const auto& jk : j.at("keyframes")) {
            PhotoModeKeyframe k;
            for (int i = 0; i < 3; i++) {
                k.pose.pos[i] = jk.at("pos").at(i).get<float>();
            }
            k.pose.yaw = jk.value("yaw", 0.0f);
            k.pose.pitch = jk.value("pitch", 0.0f);
            k.pose.roll = jk.value("roll", 0.0f);
            k.pose.fov = jk.value("fov", 45.0f);
            k.duration = jk.value("duration", 3.0f);
            keys.push_back(k);
        }

        PhotoModeTracks_Stop();
        sKeys = keys;
        sSelected = sKeys.empty() ? -1 : 0;

        *message = "Loaded " + name + " (" + std::to_string(sKeys.size()) + " keyframes).";
        if (j.value("level", (int)gCurrLevelNum) != gCurrLevelNum || j.value("area", (int)gCurrAreaIndex) != gCurrAreaIndex) {
            *message += " Warning: recorded in a different level/area.";
        }
        return true;
    } catch (const std::exception& e) {
        *message = std::string("Load failed: ") + e.what();
        return false;
    }
}

std::vector<std::string> PhotoModeTracks_ListSaved() {
    std::vector<std::string> names;
    std::error_code ec;
    fs::directory_iterator it(PhotoModeTracks_GetDirectory(), ec);
    if (ec) {
        return names;
    }
    for (const auto& entry : it) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}
