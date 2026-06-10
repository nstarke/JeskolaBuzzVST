#pragma once

// Process-wide gear scan serialization & cache.
//
// GearScanner fully LoadLibrary()s every DLL in the gear directory to call
// GetInfo(). Two scans running concurrently in one process deadlock (or crash
// with no backtrace) inside machine DllMains under the NT loader lock — and a
// DAW restoring a song creates many controller instances in the same host
// process, each of which used to start its own background scan from
// initialize(). Serialize all scans behind one shared state and cache the
// results so the directory is only walked once per process.
//
// Header-inline so the test suite can cover it.

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <vector>

#include "GearScanner.h"
#include "../buzz/MachineInterface.h"

namespace BuzzVst {

struct SharedGearScanState {
	std::mutex mutex;
	std::condition_variable cv;
	bool running = false;            // a thread is actively scanning
	bool done = false;               // dir/results below are valid
	std::string dir;                 // directory the results came from
	std::vector<GearEntry> results;  // filtered entries
};

inline SharedGearScanState& sharedGearScanState()
{
	static SharedGearScanState s;
	return s;
}

// Get the process-wide scan results for `dir`, running the scan on the calling
// thread if nobody has yet. Blocks while another thread scans; returns false
// if that scanner appears stuck (a machine DllMain that hangs) so callers
// don't pile onto a wedged loader lock.
inline bool acquireSharedGearScan(const std::string& dir, std::vector<GearEntry>& out)
{
	auto& s = sharedGearScanState();
	std::unique_lock<std::mutex> lock(s.mutex);
	for (;;) {
		if (s.done && s.dir == dir) {
			out = s.results;
			return true;
		}
		if (!s.running) break; // nobody scanning: we become the scanner
		if (s.cv.wait_for(lock, std::chrono::seconds(180)) == std::cv_status::timeout) {
			OutputDebugStringA("[BuzzBridge] Gear scan wait timed out (scanner stuck?)\n");
			return false;
		}
	}
	s.running = true;
	lock.unlock();

	GearScanner scanner;
	std::vector<GearEntry> results;
	if (scanner.Scan(dir)) {
		// Filter control/no-output helpers (MIDI out, positional-audio
		// listeners, transport sync hacks). These declare MIF_NO_OUTPUT /
		// MIF_CONTROL_MACHINE and produce no audio by design — they shouldn't
		// appear in the gear list.
		for (auto& e : scanner.GetEntries()) {
			if (e.flags & (MIF_NO_OUTPUT | MIF_CONTROL_MACHINE)) continue;
			results.push_back(e);
		}
	}

	lock.lock();
	s.results = std::move(results);
	s.dir = dir;
	s.done = true;
	s.running = false;
	out = s.results;
	lock.unlock();
	s.cv.notify_all();
	return true;
}

} // namespace BuzzVst
