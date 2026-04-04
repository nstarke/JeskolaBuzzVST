#include "BuzzWaveTable.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace BuzzVst {

BuzzWaveTable::BuzzWaveTable()
{
	for (int i = 0; i < kMaxWaveSlots; i++) {
		slots[i].loaded = false;
		slots[i].info.Flags = 0;
		slots[i].info.Volume = 1.0f;
		memset(&slots[i].level, 0, sizeof(CWaveLevel));
	}
}

BuzzWaveTable::~BuzzWaveTable()
{
	ClearAll();
}

// =========================================================================
// Minimal WAV file parser
// Supports: PCM 8-bit, 16-bit, 24-bit, 32-bit int, 32-bit float
// =========================================================================

#pragma pack(push, 1)
struct RiffHeader {
	char riff[4];       // "RIFF"
	unsigned int size;
	char wave[4];       // "WAVE"
};

struct ChunkHeader {
	char id[4];
	unsigned int size;
};

struct FmtChunk {
	unsigned short audioFormat;    // 1=PCM, 3=IEEE float
	unsigned short numChannels;
	unsigned int sampleRate;
	unsigned int byteRate;
	unsigned short blockAlign;
	unsigned short bitsPerSample;
};
#pragma pack(pop)

bool BuzzWaveTable::parseWavFile(const std::string& path, std::vector<short>& outSamples,
                                  int& outSampleRate, int& outChannels)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;

	// Read RIFF header
	RiffHeader riff;
	if (fread(&riff, sizeof(riff), 1, f) != 1 ||
	    memcmp(riff.riff, "RIFF", 4) != 0 ||
	    memcmp(riff.wave, "WAVE", 4) != 0) {
		fclose(f);
		return false;
	}

	// Find fmt and data chunks
	FmtChunk fmt = {};
	bool foundFmt = false;
	bool foundData = false;
	std::vector<unsigned char> rawData;

	while (!feof(f)) {
		ChunkHeader chunk;
		if (fread(&chunk, sizeof(chunk), 1, f) != 1)
			break;

		if (memcmp(chunk.id, "fmt ", 4) == 0) {
			size_t toRead = std::min((size_t)chunk.size, sizeof(fmt));
			if (fread(&fmt, toRead, 1, f) != 1) break;
			// Skip any extra fmt bytes
			if (chunk.size > toRead)
				fseek(f, chunk.size - (unsigned int)toRead, SEEK_CUR);
			foundFmt = true;
		}
		else if (memcmp(chunk.id, "data", 4) == 0) {
			if (chunk.size > 500000000) { fclose(f); return false; } // 500MB max
			rawData.resize(chunk.size);
			if (chunk.size > 0) {
				size_t bytesRead = fread(rawData.data(), 1, chunk.size, f);
				if (bytesRead != chunk.size) {
					// Partial read — truncate to what we got
					if (bytesRead > 0) {
						rawData.resize(bytesRead);
					} else {
						rawData.clear();
					}
				}
			}
			foundData = true;
			break; // Got data, stop
		}
		else {
			// Skip unknown chunk
			fseek(f, chunk.size, SEEK_CUR);
			// Chunks are word-aligned
			if (chunk.size & 1) fseek(f, 1, SEEK_CUR);
		}
	}

	fclose(f);

	if (!foundFmt || !foundData || rawData.empty()) return false;
	if (fmt.numChannels < 1 || fmt.numChannels > 2) return false;
	if (fmt.sampleRate < 1000 || fmt.sampleRate > 192000) return false;

	outSampleRate = (int)fmt.sampleRate;
	outChannels = (int)fmt.numChannels;

	int bytesPerSample = fmt.bitsPerSample / 8;
	if (bytesPerSample < 1 || bytesPerSample > 8) return false;

	// Guard against integer overflow: use size_t for intermediate calculations
	size_t frameBytes = (size_t)bytesPerSample * (size_t)fmt.numChannels;
	if (frameBytes == 0) return false;
	size_t numFrames = rawData.size() / frameBytes;
	if (numFrames < 1 || numFrames > 500000000) return false; // 500M frames max (~3hrs at 44.1kHz)

	size_t totalSamples = numFrames * (size_t)fmt.numChannels;
	if (totalSamples > 1000000000) return false; // 1B samples max

	// Verify raw data is large enough for the computed sample count
	size_t requiredBytes = totalSamples * (size_t)bytesPerSample;
	if (requiredBytes > rawData.size()) {
		// Truncate to what we actually have
		totalSamples = rawData.size() / (size_t)bytesPerSample;
		numFrames = totalSamples / (size_t)fmt.numChannels;
		if (numFrames < 1) return false;
		totalSamples = numFrames * (size_t)fmt.numChannels;
	}

	outSamples.resize(totalSamples);
	const unsigned char* src = rawData.data();
	int n = (int)totalSamples;

	if (fmt.audioFormat == 1) {
		// PCM integer
		if (fmt.bitsPerSample == 8) {
			for (int i = 0; i < n; i++) {
				outSamples[i] = (short)((src[i] - 128) << 8);
			}
		}
		else if (fmt.bitsPerSample == 16) {
			const short* src16 = (const short*)src;
			for (int i = 0; i < n; i++) {
				outSamples[i] = src16[i];
			}
		}
		else if (fmt.bitsPerSample == 24) {
			for (int i = 0; i < n; i++) {
				size_t idx = (size_t)i * 3;
				if (idx + 2 >= rawData.size()) break;
				int val = (int)src[idx] | ((int)src[idx + 1] << 8) | ((int)(signed char)src[idx + 2] << 16);
				outSamples[i] = (short)(val >> 8);
			}
		}
		else if (fmt.bitsPerSample == 32) {
			const int* src32 = (const int*)src;
			for (int i = 0; i < n; i++) {
				outSamples[i] = (short)(src32[i] >> 16);
			}
		}
		else {
			return false;
		}
	}
	else if (fmt.audioFormat == 3) {
		// IEEE float
		if (fmt.bitsPerSample == 32) {
			const float* srcF = (const float*)src;
			for (int i = 0; i < n; i++) {
				float val = srcF[i];
				if (val > 1.0f) val = 1.0f;
				if (val < -1.0f) val = -1.0f;
				outSamples[i] = (short)(val * 32767.0f);
			}
		}
		else if (fmt.bitsPerSample == 64) {
			const double* srcD = (const double*)src;
			int numDoubleSamples = std::min(n, (int)(rawData.size() / sizeof(double)));
			outSamples.resize(numDoubleSamples);
			for (int i = 0; i < numDoubleSamples; i++) {
				double val = srcD[i];
				if (val > 1.0) val = 1.0;
				if (val < -1.0) val = -1.0;
				outSamples[i] = (short)(val * 32767.0);
			}
			n = numDoubleSamples;
		}
		else {
			return false;
		}
	}
	else {
		return false;
	}

	return true;
}

// =========================================================================
// Public API
// =========================================================================

bool BuzzWaveTable::LoadWav(int slotIndex, const std::string& wavPath)
{
	if (slotIndex < WAVE_MIN || slotIndex > WAVE_MAX) return false;

	int idx = slotIndex - 1; // Convert to 0-based

	// Clear existing slot
	Clear(slotIndex);

	std::vector<short> samples;
	int sampleRate = 0;
	int channels = 0;

	if (!parseWavFile(wavPath, samples, sampleRate, channels))
		return false;

	if (samples.empty()) return false;

	WaveSlot& slot = slots[idx];
	slot.samples = std::move(samples);
	slot.loaded = true;
	slot.filePath = wavPath;

	// Extract filename for display name
	const char* fname = strrchr(wavPath.c_str(), '\\');
	if (!fname) fname = strrchr(wavPath.c_str(), '/');
	if (fname) fname++; else fname = wavPath.c_str();
	slot.name = fname;
	auto dotPos = slot.name.rfind('.');
	if (dotPos != std::string::npos)
		slot.name = slot.name.substr(0, dotPos);

	// Set up CWaveInfo
	slot.info.Volume = 1.0f;
	slot.info.Flags = 0;
	if (channels == 2) slot.info.Flags |= WF_STEREO;

	// Set up CWaveLevel
	int numFrames = (int)slot.samples.size() / channels;
	slot.level.numSamples = numFrames;
	slot.level.pSamples = slot.samples.data();
	slot.level.RootNote = 60; // Middle C
	slot.level.SamplesPerSec = sampleRate;
	slot.level.LoopStart = 0;
	slot.level.LoopEnd = numFrames;

	return true;
}

int BuzzWaveTable::LoadWavAuto(const std::string& wavPath)
{
	int freeSlot = GetFreeWave();
	if (freeSlot == 0) return 0; // No free slots

	if (LoadWav(freeSlot, wavPath))
		return freeSlot;

	return 0;
}

void BuzzWaveTable::Clear(int slotIndex)
{
	if (slotIndex < WAVE_MIN || slotIndex > WAVE_MAX) return;

	int idx = slotIndex - 1;
	WaveSlot& slot = slots[idx];
	slot.loaded = false;
	slot.name.clear();
	slot.filePath.clear();
	slot.samples.clear();
	slot.info.Flags = 0;
	slot.info.Volume = 1.0f;
	memset(&slot.level, 0, sizeof(CWaveLevel));
}

void BuzzWaveTable::ClearAll()
{
	for (int i = 1; i <= kMaxWaveSlots; i++)
		Clear(i);
}

const CWaveInfo* BuzzWaveTable::GetWave(int i) const
{
	if (i < WAVE_MIN || i > WAVE_MAX) return nullptr;
	int idx = i - 1;
	if (!slots[idx].loaded) return nullptr;
	return &slots[idx].info;
}

const CWaveLevel* BuzzWaveTable::GetWaveLevel(int i, int level) const
{
	if (i < WAVE_MIN || i > WAVE_MAX) return nullptr;
	int idx = i - 1;
	if (!slots[idx].loaded) return nullptr;
	// We only store one level (full resolution = level 0)
	if (level != 0) return nullptr;
	return &slots[idx].level;
}

const CWaveLevel* BuzzWaveTable::GetNearestWaveLevel(int i, int note) const
{
	// For single-sample waves, always return level 0
	return GetWaveLevel(i, 0);
}

const char* BuzzWaveTable::GetWaveName(int i) const
{
	if (i < WAVE_MIN || i > WAVE_MAX) return "";
	int idx = i - 1;
	if (!slots[idx].loaded) return "";
	return slots[idx].name.c_str();
}

int BuzzWaveTable::GetFreeWave() const
{
	for (int i = 0; i < kMaxWaveSlots; i++) {
		if (!slots[i].loaded) return i + 1; // Return 1-based index
	}
	return 0; // No free slots
}

bool BuzzWaveTable::IsLoaded(int i) const
{
	if (i < WAVE_MIN || i > WAVE_MAX) return false;
	return slots[i - 1].loaded;
}

int BuzzWaveTable::GetLoadedCount() const
{
	int count = 0;
	for (int i = 0; i < kMaxWaveSlots; i++) {
		if (slots[i].loaded) count++;
	}
	return count;
}

const WaveSlot* BuzzWaveTable::GetSlot(int i) const
{
	if (i < WAVE_MIN || i > WAVE_MAX) return nullptr;
	return &slots[i - 1];
}

std::vector<BuzzWaveTable::WavePathEntry> BuzzWaveTable::GetLoadedPaths() const
{
	std::vector<WavePathEntry> result;
	for (int i = 0; i < kMaxWaveSlots; i++) {
		if (slots[i].loaded && !slots[i].filePath.empty()) {
			result.push_back({i + 1, slots[i].filePath});
		}
	}
	return result;
}

} // namespace BuzzVst
