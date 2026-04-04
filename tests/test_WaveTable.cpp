#include <windows.h>
#include "TestFramework.h"
#include "../src/buzz/MachineInterface.h"
#include "../src/buzz/BuzzWaveTable.h"
#include "../src/buzz/BuzzCallbacks.h"

#include <cstdio>
#include <cmath>
#include <vector>

using namespace BuzzVst;

// =========================================================================
// Helper: create a minimal WAV file on disk for testing
// =========================================================================

#pragma pack(push, 1)
struct TestWavHeader {
	char riff[4];           // "RIFF"
	unsigned int fileSize;
	char wave[4];           // "WAVE"
	char fmt_[4];           // "fmt "
	unsigned int fmtSize;   // 16
	unsigned short format;  // 1=PCM, 3=float
	unsigned short channels;
	unsigned int sampleRate;
	unsigned int byteRate;
	unsigned short blockAlign;
	unsigned short bitsPerSample;
	char data[4];           // "data"
	unsigned int dataSize;
};
#pragma pack(pop)

static std::string CreateTestWav16(const char* filename, int sampleRate, int channels,
                                    const std::vector<short>& samples) {
	char tempDir[MAX_PATH];
	GetTempPathA(MAX_PATH, tempDir);
	std::string path = std::string(tempDir) + filename;

	TestWavHeader hdr = {};
	memcpy(hdr.riff, "RIFF", 4);
	memcpy(hdr.wave, "WAVE", 4);
	memcpy(hdr.fmt_, "fmt ", 4);
	hdr.fmtSize = 16;
	hdr.format = 1; // PCM
	hdr.channels = (unsigned short)channels;
	hdr.sampleRate = sampleRate;
	hdr.bitsPerSample = 16;
	hdr.blockAlign = (unsigned short)(channels * 2);
	hdr.byteRate = sampleRate * hdr.blockAlign;
	memcpy(hdr.data, "data", 4);
	hdr.dataSize = (unsigned int)(samples.size() * sizeof(short));
	hdr.fileSize = sizeof(hdr) - 8 + hdr.dataSize;

	// Adjust: fileSize should be from after "RIFF" + size field
	hdr.fileSize = sizeof(hdr) - 8;
	hdr.fileSize += 0; // already includes everything after RIFF header
	// Actually: RIFF size = total file size - 8
	unsigned int totalSize = sizeof(hdr) + hdr.dataSize;
	hdr.fileSize = totalSize - 8;

	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return "";
	fwrite(&hdr, sizeof(hdr), 1, f);
	fwrite(samples.data(), sizeof(short), samples.size(), f);
	fclose(f);
	return path;
}

static std::string CreateSineWav(const char* filename, int sampleRate, int numFrames, float freq) {
	std::vector<short> samples(numFrames);
	for (int i = 0; i < numFrames; i++) {
		float t = (float)i / (float)sampleRate;
		samples[i] = (short)(sinf(2.0f * 3.14159f * freq * t) * 32000.0f);
	}
	return CreateTestWav16(filename, sampleRate, 1, samples);
}

static std::string CreateStereoSineWav(const char* filename, int sampleRate, int numFrames) {
	std::vector<short> samples(numFrames * 2); // interleaved L/R
	for (int i = 0; i < numFrames; i++) {
		float t = (float)i / (float)sampleRate;
		samples[i * 2] = (short)(sinf(2.0f * 3.14159f * 440.0f * t) * 32000.0f);     // left
		samples[i * 2 + 1] = (short)(sinf(2.0f * 3.14159f * 880.0f * t) * 32000.0f); // right
	}
	return CreateTestWav16(filename, sampleRate, 2, samples);
}

// =========================================================================
// Basic wave table tests
// =========================================================================

TEST(WaveTable, InitiallyEmpty) {
	BuzzWaveTable wt;
	ASSERT_EQ(wt.GetLoadedCount(), 0);
	ASSERT_EQ(wt.GetFreeWave(), 1); // First free slot
}

TEST(WaveTable, GetWaveReturnsNullForEmpty) {
	BuzzWaveTable wt;
	ASSERT_NULL(wt.GetWave(1));
	ASSERT_NULL(wt.GetWave(100));
	ASSERT_NULL(wt.GetWave(200));
}

TEST(WaveTable, GetWaveOutOfRange) {
	BuzzWaveTable wt;
	ASSERT_NULL(wt.GetWave(0));   // WAVE_NO
	ASSERT_NULL(wt.GetWave(-1));
	ASSERT_NULL(wt.GetWave(201));
}

TEST(WaveTable, GetWaveLevelReturnsNullForEmpty) {
	BuzzWaveTable wt;
	ASSERT_NULL(wt.GetWaveLevel(1, 0));
}

TEST(WaveTable, GetWaveNameReturnsEmptyForEmpty) {
	BuzzWaveTable wt;
	const char* name = wt.GetWaveName(1);
	ASSERT_NOT_NULL(name);
	ASSERT_EQ(strlen(name), (size_t)0);
}

// =========================================================================
// WAV loading
// =========================================================================

TEST(WaveTable, LoadMono16BitWav) {
	std::string path = CreateSineWav("test_mono.wav", 44100, 1000, 440.0f);
	ASSERT_TRUE(!path.empty());

	BuzzWaveTable wt;
	bool ok = wt.LoadWav(1, path);
	ASSERT_TRUE(ok);
	ASSERT_TRUE(wt.IsLoaded(1));
	ASSERT_EQ(wt.GetLoadedCount(), 1);

	// Check wave info
	const CWaveInfo* info = wt.GetWave(1);
	ASSERT_NOT_NULL(info);
	ASSERT_EQ(info->Flags & WF_STEREO, 0); // mono
	ASSERT_NEAR(info->Volume, 1.0f, 0.001f);

	// Check wave level
	const CWaveLevel* level = wt.GetWaveLevel(1, 0);
	ASSERT_NOT_NULL(level);
	ASSERT_EQ(level->numSamples, 1000);
	ASSERT_NOT_NULL(level->pSamples);
	ASSERT_EQ(level->SamplesPerSec, 44100);
	ASSERT_EQ(level->RootNote, 60);

	DeleteFileA(path.c_str());
}

TEST(WaveTable, LoadStereo16BitWav) {
	std::string path = CreateStereoSineWav("test_stereo.wav", 48000, 500);
	ASSERT_TRUE(!path.empty());

	BuzzWaveTable wt;
	bool ok = wt.LoadWav(1, path);
	ASSERT_TRUE(ok);

	const CWaveInfo* info = wt.GetWave(1);
	ASSERT_NOT_NULL(info);
	ASSERT_TRUE((info->Flags & WF_STEREO) != 0);

	const CWaveLevel* level = wt.GetWaveLevel(1, 0);
	ASSERT_NOT_NULL(level);
	ASSERT_EQ(level->numSamples, 500);
	ASSERT_EQ(level->SamplesPerSec, 48000);

	DeleteFileA(path.c_str());
}

TEST(WaveTable, WaveNameFromFilename) {
	std::string path = CreateSineWav("My Cool Sample.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	wt.LoadWav(5, path);

	const char* name = wt.GetWaveName(5);
	ASSERT_NOT_NULL(name);
	ASSERT_EQ(strcmp(name, "My Cool Sample"), 0);

	DeleteFileA(path.c_str());
}

TEST(WaveTable, LoadIntoSpecificSlot) {
	std::string path = CreateSineWav("test_slot.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	wt.LoadWav(42, path);

	ASSERT_TRUE(wt.IsLoaded(42));
	ASSERT_FALSE(wt.IsLoaded(1));
	ASSERT_FALSE(wt.IsLoaded(41));
	ASSERT_FALSE(wt.IsLoaded(43));

	DeleteFileA(path.c_str());
}

TEST(WaveTable, LoadWavAuto) {
	std::string path1 = CreateSineWav("test_auto1.wav", 44100, 100, 440.0f);
	std::string path2 = CreateSineWav("test_auto2.wav", 44100, 100, 880.0f);

	BuzzWaveTable wt;
	int slot1 = wt.LoadWavAuto(path1);
	int slot2 = wt.LoadWavAuto(path2);

	ASSERT_EQ(slot1, 1);
	ASSERT_EQ(slot2, 2);
	ASSERT_TRUE(wt.IsLoaded(1));
	ASSERT_TRUE(wt.IsLoaded(2));
	ASSERT_EQ(wt.GetLoadedCount(), 2);

	DeleteFileA(path1.c_str());
	DeleteFileA(path2.c_str());
}

// =========================================================================
// Clear
// =========================================================================

TEST(WaveTable, ClearSlot) {
	std::string path = CreateSineWav("test_clear.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	wt.LoadWav(1, path);
	ASSERT_TRUE(wt.IsLoaded(1));

	wt.Clear(1);
	ASSERT_FALSE(wt.IsLoaded(1));
	ASSERT_NULL(wt.GetWave(1));
	ASSERT_NULL(wt.GetWaveLevel(1, 0));
	ASSERT_EQ(wt.GetLoadedCount(), 0);

	DeleteFileA(path.c_str());
}

TEST(WaveTable, ClearAll) {
	std::string path1 = CreateSineWav("test_ca1.wav", 44100, 100, 440.0f);
	std::string path2 = CreateSineWav("test_ca2.wav", 44100, 100, 880.0f);

	BuzzWaveTable wt;
	wt.LoadWav(1, path1);
	wt.LoadWav(5, path2);
	ASSERT_EQ(wt.GetLoadedCount(), 2);

	wt.ClearAll();
	ASSERT_EQ(wt.GetLoadedCount(), 0);
	ASSERT_FALSE(wt.IsLoaded(1));
	ASSERT_FALSE(wt.IsLoaded(5));

	DeleteFileA(path1.c_str());
	DeleteFileA(path2.c_str());
}

// =========================================================================
// GetFreeWave
// =========================================================================

TEST(WaveTable, GetFreeWaveSkipsLoaded) {
	std::string path = CreateSineWav("test_free.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	wt.LoadWav(1, path);

	int free = wt.GetFreeWave();
	ASSERT_EQ(free, 2); // Slot 1 is taken, next free is 2

	DeleteFileA(path.c_str());
}

// =========================================================================
// GetNearestWaveLevel
// =========================================================================

TEST(WaveTable, GetNearestWaveLevelReturnsLevel0) {
	std::string path = CreateSineWav("test_nearest.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	wt.LoadWav(1, path);

	// Any note should return level 0 (we only store one level)
	const CWaveLevel* level = wt.GetNearestWaveLevel(1, 60);
	ASSERT_NOT_NULL(level);
	ASSERT_EQ(level->numSamples, 100);

	// Different notes should return the same level
	ASSERT_EQ(wt.GetNearestWaveLevel(1, 48), level);
	ASSERT_EQ(wt.GetNearestWaveLevel(1, 72), level);

	DeleteFileA(path.c_str());
}

// =========================================================================
// Sample data integrity
// =========================================================================

TEST(WaveTable, SampleDataMatchesInput) {
	// Create a simple ramp wave
	std::vector<short> ramp(256);
	for (int i = 0; i < 256; i++)
		ramp[i] = (short)(i * 128 - 16384);

	std::string path = CreateTestWav16("test_ramp.wav", 44100, 1, ramp);

	BuzzWaveTable wt;
	wt.LoadWav(1, path);

	const CWaveLevel* level = wt.GetWaveLevel(1, 0);
	ASSERT_NOT_NULL(level);
	ASSERT_EQ(level->numSamples, 256);

	for (int i = 0; i < 256; i++) {
		CHECK_EQ(level->pSamples[i], ramp[i]);
	}

	DeleteFileA(path.c_str());
}

// =========================================================================
// Edge cases
// =========================================================================

TEST(WaveTable, LoadNonexistentFile) {
	BuzzWaveTable wt;
	bool ok = wt.LoadWav(1, "C:\\nonexistent\\fake.wav");
	ASSERT_FALSE(ok);
	ASSERT_FALSE(wt.IsLoaded(1));
}

TEST(WaveTable, LoadNonWavFile) {
	// Create a file that's not a WAV
	char tempDir[MAX_PATH];
	GetTempPathA(MAX_PATH, tempDir);
	std::string path = std::string(tempDir) + "not_a_wav.wav";

	FILE* f = fopen(path.c_str(), "wb");
	if (f) {
		fwrite("hello world", 1, 11, f);
		fclose(f);
	}

	BuzzWaveTable wt;
	bool ok = wt.LoadWav(1, path);
	ASSERT_FALSE(ok);

	DeleteFileA(path.c_str());
}

TEST(WaveTable, LoadInvalidSlotIndex) {
	std::string path = CreateSineWav("test_invalid.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	ASSERT_FALSE(wt.LoadWav(0, path));    // WAVE_NO
	ASSERT_FALSE(wt.LoadWav(-1, path));
	ASSERT_FALSE(wt.LoadWav(201, path));

	DeleteFileA(path.c_str());
}

TEST(WaveTable, OverwriteSlot) {
	std::string path1 = CreateSineWav("test_ow1.wav", 44100, 100, 440.0f);
	std::string path2 = CreateSineWav("test_ow2.wav", 48000, 200, 880.0f);

	BuzzWaveTable wt;
	wt.LoadWav(1, path1);
	ASSERT_EQ(wt.GetWaveLevel(1, 0)->numSamples, 100);

	// Overwrite with different file
	wt.LoadWav(1, path2);
	ASSERT_EQ(wt.GetWaveLevel(1, 0)->numSamples, 200);
	ASSERT_EQ(wt.GetWaveLevel(1, 0)->SamplesPerSec, 48000);
	ASSERT_EQ(wt.GetLoadedCount(), 1); // Still just 1 loaded

	DeleteFileA(path1.c_str());
	DeleteFileA(path2.c_str());
}

TEST(WaveTable, OnlyLevel0Supported) {
	std::string path = CreateSineWav("test_levels.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	wt.LoadWav(1, path);

	ASSERT_NOT_NULL(wt.GetWaveLevel(1, 0));
	ASSERT_NULL(wt.GetWaveLevel(1, 1));
	ASSERT_NULL(wt.GetWaveLevel(1, 5));
	ASSERT_NULL(wt.GetWaveLevel(1, 10));

	DeleteFileA(path.c_str());
}

// =========================================================================
// GetLoadedPaths for persistence
// =========================================================================

TEST(WaveTable, GetLoadedPaths) {
	std::string path1 = CreateSineWav("test_gp1.wav", 44100, 100, 440.0f);
	std::string path2 = CreateSineWav("test_gp2.wav", 44100, 100, 880.0f);

	BuzzWaveTable wt;
	wt.LoadWav(3, path1);
	wt.LoadWav(7, path2);

	auto paths = wt.GetLoadedPaths();
	ASSERT_EQ((int)paths.size(), 2);
	ASSERT_EQ(paths[0].slotIndex, 3);
	ASSERT_EQ(paths[1].slotIndex, 7);
	ASSERT_TRUE(paths[0].filePath.find("test_gp1.wav") != std::string::npos);
	ASSERT_TRUE(paths[1].filePath.find("test_gp2.wav") != std::string::npos);

	DeleteFileA(path1.c_str());
	DeleteFileA(path2.c_str());
}

// =========================================================================
// Callbacks wiring
// =========================================================================

TEST(WaveTable, CallbacksWiredToWaveTable) {
	std::string path = CreateSineWav("test_cb.wav", 44100, 100, 440.0f);

	BuzzWaveTable wt;
	wt.LoadWav(1, path);

	BuzzCallbacks cb;
	cb.waveTable = &wt;

	// GetWave via callback
	const CWaveInfo* info = cb.GetWave(1);
	ASSERT_NOT_NULL(info);

	// GetWaveLevel via callback
	const CWaveLevel* level = cb.GetWaveLevel(1, 0);
	ASSERT_NOT_NULL(level);
	ASSERT_EQ(level->numSamples, 100);

	// GetNearestWaveLevel via callback (version check still works)
	ASSERT_NOT_NULL(cb.GetNearestWaveLevel(-2, -2));
	// Normal wave access
	ASSERT_NOT_NULL(cb.GetNearestWaveLevel(1, 60));

	// GetWaveName via callback
	const char* name = cb.GetWaveName(1);
	ASSERT_NOT_NULL(name);
	ASSERT_GT((int)strlen(name), 0);

	// GetFreeWave via callback
	int free = cb.GetFreeWave();
	ASSERT_EQ(free, 2); // slot 1 taken

	DeleteFileA(path.c_str());
}

TEST(WaveTable, CallbacksNullWaveTable) {
	BuzzCallbacks cb;
	cb.waveTable = nullptr;

	// Should return safe defaults, not crash
	ASSERT_NULL(cb.GetWave(1));
	ASSERT_NULL(cb.GetWaveLevel(1, 0));
	ASSERT_EQ(cb.GetFreeWave(), 0);
	ASSERT_EQ(strlen(cb.GetWaveName(1)), (size_t)0);
}
