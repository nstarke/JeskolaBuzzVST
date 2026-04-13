#pragma once

// Structured Exception Handling (SEH) guard for calling into Buzz machine code.
// Buzz machines are old 32-bit DLLs that may have bugs. We wrap all calls
// into machine code with SEH to prevent crashes from taking down the host.
//
// MSVC build: real SEH. Requires /EHa (SEH exceptions with C++ EH).
//
// clang-mingw cross build (for WINE+yabridge): clang's i686 SEH has
// assembler-label bugs with templated lambdas (L__ehtable$...) and mingw
// headers don't tolerate -fms-extensions globally. On non-MSVC the helpers
// become passthroughs. Crash protection is lost, but under WINE+yabridge
// a machine crash only kills the bridged VST host process — yabridge
// isolates the Linux DAW from it.

#include <windows.h>

#if defined(_MSC_VER)
	#define BUZZ_SEH_TRY     __try
	#define BUZZ_SEH_EXCEPT  __except(EXCEPTION_EXECUTE_HANDLER)
	#define BUZZ_SEH_CODE()  GetExceptionCode()
#else
	#define BUZZ_SEH_TRY     if (true)
	#define BUZZ_SEH_EXCEPT  else if (false)
	#define BUZZ_SEH_CODE()  ((DWORD)0)
#endif

namespace BuzzVst {

#if defined(_MSC_VER)

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

#else // non-MSVC: passthrough (see header comment)

template<typename Func>
inline bool SEH_Call(Func fn) { fn(); return true; }

template<typename T, typename Func>
inline T SEH_CallRet(Func fn, T defaultVal) { return fn(); }

#endif

} // namespace BuzzVst
