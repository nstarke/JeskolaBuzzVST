#pragma once

// Structured Exception Handling (SEH) guard for calling into Buzz machine code.
// Buzz machines are old 32-bit DLLs that may have bugs. We wrap all calls
// into machine code with SEH to prevent crashes from taking down the host.
//
// IMPORTANT: Requires /EHa compiler flag (SEH exceptions with C++ EH).
// The lambda passed to SEH_Call must not have C++ objects with destructors
// on its stack if compiled without /EHa.

#include <windows.h>

namespace BuzzVst {

// Execute a callable with SEH protection. Returns true if no exception occurred.
template<typename Func>
inline bool SEH_Call(Func fn)
{
	__try {
		fn();
		return true;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		OutputDebugStringA("[BuzzBridgeHost32] SEH exception caught in machine code\n");
		return false;
	}
}

// Execute a callable with SEH protection, returning a value.
// Returns defaultVal if an exception occurs.
template<typename T, typename Func>
inline T SEH_CallRet(Func fn, T defaultVal)
{
	__try {
		return fn();
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		OutputDebugStringA("[BuzzBridgeHost32] SEH exception caught in machine code\n");
		return defaultVal;
	}
}

} // namespace BuzzVst
