#pragma once

// Per-machine workarounds for buggy Buzz machine DLLs.
//
// A handful of old Buzz machines have bugs in their CreateMachine/Init/
// destructor paths that would crash or hang our host. Instead of hooking
// each machine's compiled code, we apply targeted patches from the outside:
//
//   - ApplyPostCreateQuirks: zero uninitialized derived-class fields (with
//     optional preserve-offset list for sub-object vtables the ctor set),
//     or null specific fields that a virtual will `operator_delete` on.
//
//   - HasBadDestructor: machines whose destructor crashes (double-free,
//     uninit pointer deref, stack overflow). The loader skips
//     `delete pMachine` and leaks the HMODULE so the DLL's static-CRT
//     atexit handlers stay callable.
//
//   - IsUnsupportedByLoadPath: machines whose CreateMachine unavoidably
//     corrupts the heap. The loader refuses to Load() them.
//
//   - QuirkedInstanceSize: the exact size each quirked machine passed to
//     operator_new, used (a) to bound ApplyPostCreateQuirks' memset and
//     (b) to verify that a ctor-provided AttrVals pointer points into the
//     machine instance rather than at garbage.
//
// These helpers are header-inline so the test suite can cover them.
// Do NOT add a machine to any of these lists unless you've traced the
// specific broken behavior from a decompile — blanket-zeroing a well-
// behaved machine wipes out oscillator tables / filter coefficients and
// silences it.

#include <windows.h>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include "MachineInterface.h"

namespace BuzzVst {

// --- name helpers ----------------------------------------------------------

inline bool QuirkNameEquals(const char* s, const char* needle)
{
	return s && needle && _stricmp(s, needle) == 0;
}

inline bool QuirkNameContains(const char* s, const char* needle)
{
	if (!s || !needle) return false;
	size_t nlen = strlen(needle);
	for (const char* p = s; *p; p++) {
		if (_strnicmp(p, needle, nlen) == 0) return true;
	}
	return false;
}

// --- per-machine tables ----------------------------------------------------

// Returns the exact size in bytes that a quirked machine's CreateMachine
// passed to operator_new, or 0 if the machine has no known quirk.
inline SIZE_T QuirkedInstanceSize(const CMachineInfo* info)
{
	if (!info) return 0;
	if (QuirkNameEquals(info->Name, "Jeskola Cross Delay") ||
	    QuirkNameEquals(info->ShortName, "Cross Delay")) return 0x18c;
	if (QuirkNameEquals(info->Name, "Jeskola Delay")) return 0x11c;
	if (QuirkNameContains(info->Name, "Frequency Bomb") ||
	    QuirkNameContains(info->ShortName, "Frequency Bomb") ||
	    QuirkNameContains(info->ShortName, "Freq Bomb")) return 0x90;
	if (QuirkNameContains(info->Name, "Rosengarten") ||
	    QuirkNameContains(info->ShortName, "Rosengarten")) return 0x66c;
	if (QuirkNameEquals(info->Name, "Automaton DC Eliminator")) return 0x2d0;
	if (QuirkNameEquals(info->Name, "Zephod PolyWog 22")) return 0x100;
	if (QuirkNameEquals(info->Name, "Static Duafilt II")) return 0xc8;
	if (QuirkNameEquals(info->Name, "7900s Pearl Drum")) return 0x29790;
	return 0;
}

// Machines whose CreateMachine calls a member function that reads-then-frees
// an uninitialized pointer before zero-initializing it. The only way to make
// these safe would be to intercept the DLL's operator_new to hand back
// zeroed memory — too invasive for the handful of affected machines. We
// refuse to load them so the test records LOAD FAIL instead of a heap-
// corruption crash that taints later allocations.
inline bool IsUnsupportedByLoadPath(const CMachineInfo* info)
{
	if (!info) return false;
	// Lost_Bit iPan: sub-object ctor reads [sub+8] before zeroing it,
	// calling free() on heap garbage.
	if (QuirkNameContains(info->Name, "Lost_Bit iPan")) return true;
	return false;
}

// Machines whose destructor is known to crash (double-free, uninit pointer
// deref, stack overflow). For these the loader skips `delete pMachine` and
// the DLL HMODULE is leaked so the DLL's atexit handlers remain callable.
inline bool HasBadDestructor(const CMachineInfo* info)
{
	if (!info) return false;
	// Zephod PolyWog 22: destructor double-frees [esi+0xB8] (both lines
	// 100010BB and 100010C7 push the same pointer).
	if (QuirkNameEquals(info->Name, "Zephod PolyWog 22")) return true;
	// Static Duafilt II: destructor crashes after Work succeeds (heap
	// corruption detected at CRT exit).
	if (QuirkNameEquals(info->Name, "Static Duafilt II")) return true;
	// 7900s Pearl Drum: destructor crashes with heap corruption after Work.
	if (QuirkNameEquals(info->Name, "7900s Pearl Drum")) return true;
	// kazuya JoyControl-4: heap corruption in destructor.
	if (QuirkNameContains(info->Name, "JoyControl")) return true;
	// Zephod ReSaw: heap corruption in destructor.
	if (QuirkNameEquals(info->Name, "Zephod ReSaw")) return true;
	// Harcs Vision: heap corruption in destructor that only became
	// detectable after our stack layout changed.
	if (QuirkNameEquals(info->Name, "Harcs Vision")) return true;
	return false;
}

// --- quirk application -----------------------------------------------------

// Zero [startOff..endOff) of a buffer, EXCEPT for 4-byte words at each
// `preserveOffsets[i]`. Used for minimal-ctor machines whose ctor also set
// a sub-object vtable at a derived-class offset we must not wipe.
inline void ZeroRangePreserving(char* base, SIZE_T startOff, SIZE_T endOff,
                                 const SIZE_T* preserveOffsets, size_t numPreserves)
{
	uint32_t saved[16] = {0};
	size_t nsave = (numPreserves < 16) ? numPreserves : 16;
	for (size_t i = 0; i < nsave; i++) {
		saved[i] = *reinterpret_cast<uint32_t*>(base + preserveOffsets[i]);
	}
	memset(base + startOff, 0, endOff - startOff);
	for (size_t i = 0; i < nsave; i++) {
		*reinterpret_cast<uint32_t*>(base + preserveOffsets[i]) = saved[i];
	}
}

// Post-CreateMachine dispatcher. Applies a per-machine memory fixup so the
// machine's Init/Work/destructor don't crash on uninitialized state. Call
// BEFORE setting pMasterInfo/pCB/AttrVals so those base-class slots are
// preserved (they live at offsets 0x10, 0x14, and 0xc — all below 0x18).
inline void ApplyPostCreateQuirks(CMachineInterface* pMachine, const CMachineInfo* info)
{
	if (!pMachine || !info) return;
	char* base = reinterpret_cast<char*>(pMachine);
	constexpr SIZE_T kBaseClassSize = 6 * sizeof(void*); // 0x18

	// ---- Minimal-ctor machines ----
	struct MinimalCtorQuirk {
		SIZE_T size;
		SIZE_T preserves[4];
		size_t numPreserves;
	};
	MinimalCtorQuirk q = { 0, {0}, 0 };
	if (QuirkNameEquals(info->Name, "Jeskola Cross Delay") ||
	    QuirkNameEquals(info->ShortName, "Cross Delay")) {
		q = { 0x18c, {0}, 0 };
	} else if (QuirkNameEquals(info->Name, "Jeskola Delay")) {
		q = { 0x11c, {0}, 0 };
	} else if (QuirkNameContains(info->Name, "Frequency Bomb") ||
	           QuirkNameContains(info->ShortName, "Frequency Bomb") ||
	           QuirkNameContains(info->ShortName, "Freq Bomb")) {
		q = { 0x90, {0}, 0 };
	} else if (QuirkNameEquals(info->Name, "Zephod PolyWog 22")) {
		q = { 0x100, {0}, 0 };
	} else if (QuirkNameEquals(info->Name, "Static Duafilt II")) {
		// Preserve sub-object vtable the ctor sets at +0x1c.
		q = { 0xc8, {0x1c, 0, 0, 0}, 1 };
	} else if (QuirkNameEquals(info->Name, "7900s Pearl Drum")) {
		// Preserve sub-object vtable the ctor sets at +0x20.
		q = { 0x29790, {0x20, 0, 0, 0}, 1 };
	}
	if (q.size > kBaseClassSize) {
		ZeroRangePreserving(base, kBaseClassSize, q.size, q.preserves, q.numPreserves);
		return;
	}

	// ---- Automaton DC Eliminator ----
	// Ctor fully initializes its 10 per-stage biquad sub-objects, but
	// leaves this+0x18 (an IConnectionPoint backpointer the destructor
	// virtual-calls). Null it so the destructor is safe. The attribute
	// default at this+0x2cc gets written via the AttrVals-inside-instance
	// path once QuirkedInstanceSize returns non-zero.
	if (QuirkNameEquals(info->Name, "Automaton DC Eliminator")) {
		*reinterpret_cast<void**>(base + 0x18) = nullptr;
		return;
	}

	// ---- Rosengarten ----
	// 8 per-track sub-objects at this+0xcc, 0xb4 bytes each. The sub-
	// object ctor only sets its vtable; buffer pointer fields at sub+4
	// and sub+8 are garbage. Init invokes a virtual that does
	// operator_delete on both before reallocating. Null the 16 pointers.
	if (QuirkNameContains(info->Name, "Rosengarten") ||
	    QuirkNameContains(info->ShortName, "Rosengarten")) {
		for (int t = 0; t < 8; t++) {
			char* sub = base + 0xcc + t * 0xb4;
			*reinterpret_cast<void**>(sub + 4) = nullptr;
			*reinterpret_cast<void**>(sub + 8) = nullptr;
		}
		return;
	}
}

} // namespace BuzzVst
