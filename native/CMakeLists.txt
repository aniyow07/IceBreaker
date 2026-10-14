cmake_minimum_required(VERSION 3.22)
project(icebreaker_compat CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- dynarmic (A32 frontend only) -----------------------------------------------------
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(Boost_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost CACHE PATH "" FORCE)
add_subdirectory(third_party/dynarmic EXCLUDE_FROM_ALL)

# --- compatibility layer ---------------------------------------------------------------------
set(COMPAT_SOURCES
    compat/guest_mem.cpp compat/cpu.cpp compat/elf_loader.cpp compat/hle_core.cpp
    compat/hle_stdio.cpp compat/hle_sys.cpp compat/hle_zlib.cpp compat/hle_gl.cpp
    compat/jni_bridge.cpp compat/zip_reader.cpp compat/hle_hooks.cpp compat/runtime.cpp compat/java_exports.cpp)

if(ANDROID)
    add_library(game SHARED ${COMPAT_SOURCES} compat/gl_android.cpp)
    target_link_libraries(game PRIVATE dynarmic GLESv1_CM z log)
    target_link_options(game PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
    target_compile_options(game PRIVATE -O2 -fvisibility=hidden)
    # Empty stand-in so the original Java code's System.loadLibrary("cocosdenshion") succeeds;
    # the real (ARM32) libcocosdenshion.so runs inside the compatibility layer.
    add_library(cocosdenshion SHARED compat/cocosdenshion_stub.cpp)
    target_link_options(cocosdenshion PRIVATE -Wl,-z,max-page-size=16384)
else()
    # PC test harness (x86-64): same layer, desktop OpenGL, an embedded JVM for the Java side.
    if(NOT DEFINED JDK_HOME)
        message(FATAL_ERROR "set -DJDK_HOME=<path to a JDK>")
    endif()
    add_executable(compat_harness ${COMPAT_SOURCES} harness/main.cpp harness/gl_wgl.cpp harness/png_write.cpp)
    target_include_directories(compat_harness PRIVATE ${JDK_HOME}/include ${JDK_HOME}/include/win32
                               ${ZLIB_INCLUDE_DIR} compat)
    target_link_libraries(compat_harness PRIVATE dynarmic ${ZLIB_LIBRARY} opengl32 gdi32 user32)
endif()
