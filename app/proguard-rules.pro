# Add project specific ProGuard rules here.
# isMinifyEnabled is false by default (see app/build.gradle.kts) so this
# file is currently inert; kept so release builds can turn minification on
# later without first having to create it.

# Keep JNI entry points and the classes they construct/throw reflectively
# (see native/jni/arc_extract_jni.cpp) — R8 cannot see those references.
-keep class eu.lielu.arcextract.jni.** { *; }
