#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace BuzzVst {

// Processor UIDs
static const Steinberg::FUID kBuzzGeneratorProcessorUID(0x4A45534B, 0x4F4C4142, 0x555A5A47, 0x454E3031);
static const Steinberg::FUID kBuzzEffectProcessorUID   (0x4A45534B, 0x4F4C4142, 0x555A5A46, 0x583031AA);

// Controller UIDs
static const Steinberg::FUID kBuzzGeneratorControllerUID(0x4A45534B, 0x4F4C4142, 0x555A5A47, 0x43544C31);
static const Steinberg::FUID kBuzzEffectControllerUID   (0x4A45534B, 0x4F4C4142, 0x555A5A46, 0x43544C32);

// Parameter ID ranges
// IDs 1..128 are Buzz global parameters (pre-allocated, hidden until loaded)
// IDs 1000+T*1000..1000+T*1000+63 are Buzz track T parameters
//   Track 0: 1000-1063, Track 1: 2000-2063, ..., Track 15: 16000-16063
enum ReservedParamIDs {
	kBuzzGlobalParamBase = 1,
	kMaxGlobalParams = 128,
	kBuzzTrackParamBase = 1000,
	kTrackParamStride = 1000,     // ID spacing between tracks
	kMaxTrackParams = 64,         // max params per track
	kMaxTracks = 16,              // max number of tracks
	kBypassParamID = 50000,
};

// Version string — set by CMake from the git tag, or fallback
#ifndef BUZZVST_VERSION_STR
#define BUZZVST_VERSION_STR "0.0.0.0"
#endif

// Audio scale factor: Buzz uses ±32768, VST3 uses ±1.0
static const float kBuzzToVst3Scale = 1.0f / 32768.0f;
static const float kVst3ToBuzzScale = 32768.0f;

// Clamp a Buzz-scale sample to VST3 range, guarding against NaN/Inf from buggy machines
inline float BuzzSampleToVst3(float buzzSample) {
	float v = buzzSample * kBuzzToVst3Scale;
	if (v > 1.0f) v = 1.0f;
	if (v < -1.0f) v = -1.0f;
	// Guard against NaN (NaN != NaN)
	if (v != v) v = 0.0f;
	return v;
}

} // namespace BuzzVst
