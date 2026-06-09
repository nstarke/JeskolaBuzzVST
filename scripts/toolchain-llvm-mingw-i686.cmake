# CMake toolchain file for cross-compiling BuzzBridge to 32-bit Windows
# using llvm-mingw (clang + mingw-w64 + lld). Targets i686-w64-windows-gnu.
#
# Why llvm-mingw rather than gcc-mingw-w64:
#   - clang supports __try/__except SEH on i686 (used by src/common/SEHGuard.h).
#   - lld handles the VST3 SDK's resource/def files cleanly.
#
# Pass via:  cmake -DCMAKE_TOOLCHAIN_FILE=scripts/toolchain-llvm-mingw-i686.cmake
# LLVM_MINGW_ROOT must point at an unpacked llvm-mingw release.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

if(NOT DEFINED ENV{LLVM_MINGW_ROOT})
    message(FATAL_ERROR "LLVM_MINGW_ROOT env var must point at an llvm-mingw install")
endif()
set(_LLVM_MINGW "$ENV{LLVM_MINGW_ROOT}")

set(_TRIPLE i686-w64-mingw32)

set(CMAKE_C_COMPILER   "${_LLVM_MINGW}/bin/${_TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${_LLVM_MINGW}/bin/${_TRIPLE}-clang++")
set(CMAKE_RC_COMPILER  "${_LLVM_MINGW}/bin/${_TRIPLE}-windres")
set(CMAKE_AR           "${_LLVM_MINGW}/bin/llvm-ar"      CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_LLVM_MINGW}/bin/llvm-ranlib"  CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH "${_LLVM_MINGW}/${_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --target pins the triple so clang picks the mingw sysroot.
#
# NOTE: -fms-extensions / -fms-compatibility are NOT set here. Applying them
# globally breaks mingw's vadefs.h (VARARGS error) and disables clang's
# builtin char16_t/char32_t, causing the VST3 SDK and libc++ to fail to
# compile. Those flags are scoped to the SEH-using TUs in the top-level
# CMakeLists.txt instead.
set(_COMMON_FLAGS "--target=${_TRIPLE}")
set(CMAKE_C_FLAGS_INIT   "${_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_COMMON_FLAGS}")

# VST3 SDK expects this to be defined on Windows builds.
add_compile_definitions(WIN32 _WINDOWS SMTG_OS_WINDOWS=1)

# libc++ shipped with llvm-mingw expects C11 aligned_alloc, which ucrt's
# stdlib.h does not expose — it only has _aligned_malloc. Tell libc++ not
# to try. Without this, <cstdlib>/<utility> fail to compile.
add_compile_definitions(_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1)

# Statically link the llvm-mingw C++ runtime (libc++), the unwinder
# (libunwind) and compiler-rt into every artifact. Without this the produced
# BuzzBridge.vst3 / .exe files import libc++.dll and libunwind.dll, which are
# NOT present in a stock WINE prefix — so the plugin would fail to load under
# yabridge with status c0000135 (missing DLL). -static makes each binary
# self-contained; only system DLLs (kernel32, user32, ...) remain as imports.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static")
