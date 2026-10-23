#pragma once

#include <string>
#include <vector>

#include "PhotoMode.h"

// Camera tracks: keyframed camera paths that play back smoothly through a level.

struct PhotoModeKeyframe {
    PhotoModePose pose;
    // Seconds to travel from this keyframe to the next one (ignored on the last keyframe).
    float duration = 3.0f;
};

enum class PhotoModeTrackEasing {
    // Constant pace within each segment; never stops at keyframes.
    Linear,
    // Accelerates out of the first keyframe and slows into the last one.
    EaseStartEnd,
    // Slows to a stop at every keyframe.
    EaseEachKeyframe,
};

std::vector<PhotoModeKeyframe>& PhotoModeTracks_Keyframes();
int PhotoModeTracks_GetSelected();
void PhotoModeTracks_SetSelected(int index);

// Inserts a keyframe after the selected one (or at the end) and selects it.
void PhotoModeTracks_AddKeyframe(const PhotoModePose& pose);
void PhotoModeTracks_DeleteKeyframe(int index);
void PhotoModeTracks_MoveKeyframe(int index, int direction);
void PhotoModeTracks_Clear();
// Moves the camera to a keyframe and selects it.
void PhotoModeTracks_JumpTo(int index);

bool PhotoModeTracks_CanPlay();
void PhotoModeTracks_Play();
void PhotoModeTracks_Stop();
void PhotoModeTracks_TogglePlay();
bool PhotoModeTracks_IsPlaying();          // true during the countdown too
float PhotoModeTracks_CountdownRemaining(); // seconds; 0 once the camera is moving
float PhotoModeTracks_GetTime();
float PhotoModeTracks_GetDuration();

// Called once per game frame by the camera while playing. Returns false when not playing.
bool PhotoModeTracks_Advance(PhotoModePose* out);
PhotoModePose PhotoModeTracks_Evaluate(float time);
// Samples along the path for drawing a preview.
std::vector<PhotoModePose> PhotoModeTracks_SamplePath(int samplesPerSegment);

// Saved tracks live in <game folder>/photomode_tracks/<name>.json.
bool PhotoModeTracks_Save(const std::string& name, std::string* message);
bool PhotoModeTracks_Load(const std::string& name, std::string* message);
std::vector<std::string> PhotoModeTracks_ListSaved();
std::string PhotoModeTracks_GetDirectory();
