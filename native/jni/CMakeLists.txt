# Only ever included from the parent native/CMakeLists.txt when
# ANDROID_ABI is set (i.e. real NDK build). This defines the shared
# library Kotlin loads with System.loadLibrary("arcextract_jni")
# (app/src/main/java/eu/lielu/arcextract/jni/ArcNative.kt).
#
# Verified so far (see docs/ANALYSIS.md §7 and §9): arc_extract_jni.cpp
# compiles cleanly against a real jni.h with a desktop g++. NOT verified:
# an actual NDK cross-compile/link for arm64-v8a, since no NDK is
# installed in this sandbox. Try it with:
#   cmake -S native -B native/build-android \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
#   cmake --build native/build-android

add_library(arcextract_jni SHARED
    arc_extract_jni.cpp
)

target_link_libraries(arcextract_jni PRIVATE arcextract_arc)

find_library(log-lib log)
if(log-lib)
    target_link_libraries(arcextract_jni PRIVATE ${log-lib})
endif()

# --- Not yet linked in: the codec backends that need vendored sources ---
# Once native/third_party/innoextract (MIT/zlib) and
# native/third_party/freearc (GPL-2.0) have real decode call-sites wired
# (see docs/BUILDING.md "Prochaines étapes"), their static libraries get
# added here, e.g.:
#   add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/innoextract innoextract_build)
#   target_link_libraries(arcextract_jni PRIVATE innoextract_lib)
# innoextract additionally needs Boost + liblzma for Android — see
# docs/ANALYSIS.md §2 for why alanwoolley/innoextract-android is the
# reference for how that dependency was solved before.
