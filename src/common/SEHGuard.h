#pragma once

// Structured Exception Handling (SEH) guard for calling into Buzz machine code.
// Buzz machines are old 32-bit DLLs that may have bugs. We wrap all calls
// into machine code with a crash guard to prevent a faulting machine from
// taking down the host process.
//
// Three implementations, selected at compile time:
//
//   MSVC build (native Windows):
//       Real SEH (__try/__except). Requires /EHa. Frame-based, so a machine's
//       own internal __try/__except runs before ours — the ideal behaviour.
//
//   clang-mingw cross build (for WINE + yabridge on Linux):
//       Real i686 SEH is unusable here — clang's i686 __try emits broken
//       assembler labels (L__ehtable$...) for templated lambdas and mingw's
//       headers don't tolerate -fms-extensions globally. Instead we install a
//       process-wide Vectored Exception Handler (VEH) and longjmp out of it
//       back to a per-call setjmp landing pad. AddVectoredExceptionHandler is
//       a plain Win32 API that works under WINE regardless of compiler, so a
//       crashing Buzz machine is caught and the host survives. See caveat (*).
//
//   Any other compiler/target:
//       Passthrough (no protection).
//
// (*) VEH is a *first-chance* handler: it runs before frame-based SEH. We only
//     act when one of our guards is active and the exception is a hard fault
//     (access violation, illegal instruction, integer divide-by-zero, ...), so
//     ordinary control flow is undisturbed. The rare machine that relies on its
//     own internal __try/__except to recover from such a fault would be
//     preempted, but in practice Buzz machines do not — they simply crash,
//     which is exactly what this guard exists to contain. Stack-overflow and
//     C++/debug exceptions are deliberately left for normal handling.

#include <windows.h>
#if defined(__MINGW32__) && !defined(_MSC_VER)
	#include <csetjmp>
#endif

#if defined(_MSC_VER)
	#define BUZZ_SEH_TRY     __try
	#define BUZZ_SEH_EXCEPT  __except(EXCEPTION_EXECUTE_HANDLER)
	#define BUZZ_SEH_CODE()  GetExceptionCode()
#elif defined(__MINGW32__)
	// Defined below in terms of the VEH guard scope.
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

#elif defined(__MINGW32__) // clang-mingw / WINE: VEH + setjmp (see header comment)

namespace sehdetail {

// One landing pad per active guard, threaded into a per-thread LIFO stack so
// guards can nest. On a fault the VEH handler pops the innermost frame and
// longjmps to its setjmp pad.
struct GuardFrame {
	jmp_buf      jb;
	GuardFrame*  prev;
	DWORD        code;
};

inline thread_local GuardFrame* g_top = nullptr;

// Hard faults we treat as "machine crashed". Deliberately excludes
// STACK_OVERFLOW (no stack left to longjmp on safely), breakpoints/single-step
// (debugging) and the MSVC C++ throw code 0xE06D7363.
inline bool IsFatalCrash(DWORD code)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_INVALID_OPERATION:
	case EXCEPTION_FLT_STACK_CHECK:
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		return true;
	default:
		return false;
	}
}

inline LONG CALLBACK VehHandler(EXCEPTION_POINTERS* ep)
{
	GuardFrame* top = g_top;
	const DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (top && IsFatalCrash(code)) {
		top->code = code;
		g_top = top->prev;          // pop before we leave the handler
		longjmp(top->jb, 1);        // does not return
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

// Register the VEH exactly once per process. Returns true if installed.
// Setting BUZZ_NO_VEH in the environment disables registration (debug escape
// hatch / passthrough behaviour).
inline bool EnsureHandler()
{
	static PVOID handle =
		getenv("BUZZ_NO_VEH") ? nullptr
		                      : AddVectoredExceptionHandler(1 /*first*/, VehHandler);
	return handle != nullptr;
}

// RAII arm/disarm of a guard frame. Constructed = armed (pushed onto g_top);
// destroyed = disarmed (popped). longjmp skips the destructor, so VehHandler
// pops the frame itself; the later destructor pop is then idempotent.
struct GuardScope {
	GuardFrame frame;
	GuardScope()
	{
		EnsureHandler();
		frame.prev = g_top;
		frame.code = 0;
		g_top = &frame;             // arm before setjmp at the call site
	}
	~GuardScope() { g_top = frame.prev; }

	GuardScope(const GuardScope&) = delete;
	GuardScope& operator=(const GuardScope&) = delete;
};

} // namespace sehdetail

// NOTE: setjmp must appear textually in the same function as the protected
// code (its environment becomes invalid once that function returns), and as
// the entire controlling expression of the `if`, so it cannot be hidden inside
// a helper. The GuardScope arms the frame in its constructor so the condition
// stays a bare `setjmp(...) == 0`.

template<typename Func>
inline bool SEH_Call(Func fn)
{
	sehdetail::GuardScope s;
	if (setjmp(s.frame.jb) == 0) {
		fn();
		return true;
	}
	OutputDebugStringA("[BuzzBridgeHost32] VEH crash caught in machine code\n");
	return false;
}

template<typename T, typename Func>
inline T SEH_CallRet(Func fn, T defaultVal)
{
	sehdetail::GuardScope s;
	if (setjmp(s.frame.jb) == 0) {
		return fn();
	}
	OutputDebugStringA("[BuzzBridgeHost32] VEH crash caught in machine code\n");
	return defaultVal;
}

// Block-form guard mirroring __try/__except. _bz_seh_scope stays in scope for
// both the try and except blocks (if-with-initializer), so BUZZ_SEH_CODE() can
// read the captured fault code inside the except block.
#define BUZZ_SEH_TRY \
	if (BuzzVst::sehdetail::GuardScope _bz_seh_scope; \
	    setjmp(_bz_seh_scope.frame.jb) == 0)
#define BUZZ_SEH_EXCEPT  else
#define BUZZ_SEH_CODE()  (_bz_seh_scope.frame.code)

#else // non-MSVC, non-mingw: passthrough (see header comment)

template<typename Func>
inline bool SEH_Call(Func fn) { fn(); return true; }

template<typename T, typename Func>
inline T SEH_CallRet(Func fn, T defaultVal) { return fn(); }

#endif

} // namespace BuzzVst
