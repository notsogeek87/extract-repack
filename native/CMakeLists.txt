cmake_minimum_required(VERSION 3.22)
project(arcextract_native CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This file builds two things from the same sources:
#  1. A desktop target (arc_probe + tests), used to verify native/arc/ with
#     a plain host compiler — no Android SDK/NDK needed. This is how the
#     code in this repository was actually compiled and tested during
#     development (see docs/ANALYSIS.md §7).
#  2. When invoked by the Android Gradle Plugin's externalNativeBuild
#     (android.toolchain.cmake is on the CMAKE_TOOLCHAIN_FILE), the same
#     arcextract_arc library additionally becomes the target the JNI
#     bridge (native/jni/) links against for the real app build. No
#     x86-only assembly is compiled in either case — see
#     docs/ANALYSIS.md §3.3 on why that matters for third_party/freearc.

add_library(arcextract_arc STATIC
    arc/vle.cpp
    arc/crc32.cpp
    arc/file_source.cpp
    arc/arc_reader.cpp
)
target_include_directories(arcextract_arc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(arc_probe tools/arc_probe.cpp)
target_link_libraries(arc_probe PRIVATE arcextract_arc)

enable_testing()
add_executable(arc_reader_test tests/arc_reader_test.cpp)
target_link_libraries(arc_reader_test PRIVATE arcextract_arc)
add_test(
    NAME arc_reader_test
    COMMAND arc_reader_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# --- Android/JNI target -----------------------------------------------
# Built only when configuring for Android (ANDROID_ABI is set by
# android.toolchain.cmake). See native/jni/ and docs/BUILDING.md for what
# is and is not wired yet.
if(ANDROID_ABI)
    add_subdirectory(jni)
endif()
