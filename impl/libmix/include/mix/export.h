#pragma once

// Symbol visibility macros for shared library builds
// Usage: MIX_API on public functions, MIX_LOCAL on internal functions

#if defined(MIX_STATIC)
    // Static library - no special visibility needed
    #define MIX_API
    #define MIX_LOCAL
#elif defined(_WIN32) || defined(__CYGWIN__)
    // Windows
    #ifdef MIX_BUILDING
        #define MIX_API __declspec(dllexport)
    #else
        #define MIX_API __declspec(dllimport)
    #endif
    #define MIX_LOCAL
#elif defined(__GNUC__) && __GNUC__ >= 4
    // GCC/Clang with visibility support
    #define MIX_API   __attribute__((visibility("default")))
    #define MIX_LOCAL __attribute__((visibility("hidden")))
#else
    // Fallback - no visibility control
    #define MIX_API
    #define MIX_LOCAL
#endif

// C linkage helper for C API
#ifdef __cplusplus
    #define MIX_EXTERN_C       extern "C"
    #define MIX_EXTERN_C_BEGIN extern "C" {
    #define MIX_EXTERN_C_END   }
#else
    #define MIX_EXTERN_C
    #define MIX_EXTERN_C_BEGIN
    #define MIX_EXTERN_C_END
#endif
