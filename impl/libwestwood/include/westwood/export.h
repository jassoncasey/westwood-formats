#pragma once

// Symbol visibility macros for shared library builds

#if defined(WWD_STATIC)
    #define WWD_API
    #define WWD_LOCAL
#elif defined(_WIN32) || defined(__CYGWIN__)
    #ifdef WWD_BUILDING
        #define WWD_API __declspec(dllexport)
    #else
        #define WWD_API __declspec(dllimport)
    #endif
    #define WWD_LOCAL
#elif defined(__GNUC__) && __GNUC__ >= 4
    #define WWD_API   __attribute__((visibility("default")))
    #define WWD_LOCAL __attribute__((visibility("hidden")))
#else
    #define WWD_API
    #define WWD_LOCAL
#endif

#ifdef __cplusplus
    #define WWD_EXTERN_C       extern "C"
    #define WWD_EXTERN_C_BEGIN extern "C" {
    #define WWD_EXTERN_C_END   }
#else
    #define WWD_EXTERN_C
    #define WWD_EXTERN_C_BEGIN
    #define WWD_EXTERN_C_END
#endif
