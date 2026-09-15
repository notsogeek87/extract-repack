// JNI bridge between Kotlin's `eu.lielu.arcextract.jni.ArcNative`
// (app/src/main/java/eu/lielu/arcextract/jni/ArcNative.kt) and the
// platform-independent container parser in native/arc/. See
// docs/ANALYSIS.md §6 for the overall architecture and §"Prochaines
// étapes" in docs/BUILDING.md for exactly what is and is not wired here.
//
// Deliberately thin: every actual format-parsing decision lives in
// native/arc/arc_reader.cpp, which has its own standalone test suite
// (native/tests/) runnable without any Android tooling. This file only
// translates between JNI types and that engine, and turns C++ exceptions
// into the specific Kotlin exception types ArcNative.kt documents.
//
// Verified in this session: this file compiles cleanly against a real
// jni.h (the desktop JDK's, headers-only — see docs/ANALYSIS.md §7). It
// has NOT been linked or run against an actual JVM/ART, and not compiled
// with the Android NDK toolchain at all, since neither is available in
// this sandbox.
#include <jni.h>

#include <string>
#include <vector>

#include "../arc/arc_reader.h"

using namespace arcextract;

namespace {

void throwByClassName(JNIEnv* env, const char* className, const std::string& message) {
    jclass cls = env->FindClass(className);
    if (cls != nullptr) {
        env->ThrowNew(cls, message.c_str());
    }
}

void throwUnsupportedCompressor(JNIEnv* env, const std::string& compressorId) {
    jclass cls = env->FindClass("eu/lielu/arcextract/jni/UnsupportedCompressorException");
    if (cls == nullptr) return;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;)V");
    if (ctor == nullptr) return;
    jstring idStr = env->NewStringUTF(compressorId.c_str());
    jobject exc = env->NewObject(cls, ctor, idStr);
    if (exc != nullptr) {
        env->Throw(static_cast<jthrowable>(exc));
    }
}

void throwArcCorrupted(JNIEnv* env, const std::string& message) {
    throwByClassName(env, "eu/lielu/arcextract/jni/ArcCorruptedException", message);
}

void throwIOException(JNIEnv* env, const std::string& message) {
    throwByClassName(env, "java/io/IOException", message);
}

} // namespace

extern "C" JNIEXPORT jobjectArray JNICALL
Java_eu_lielu_arcextract_jni_ArcNative_listArc(JNIEnv* env, jobject /*thiz*/, jint fd) {
    std::vector<ArcEntry> entries;
    try {
        ArcReader reader(static_cast<int>(fd));
        entries = reader.list();
    } catch (const UnsupportedCompressorError& e) {
        throwUnsupportedCompressor(env, e.compressorId());
        return nullptr;
    } catch (const ArcFormatError& e) {
        throwArcCorrupted(env, e.what());
        return nullptr;
    } catch (const std::exception& e) {
        throwIOException(env, e.what());
        return nullptr;
    }

    jclass entryClass = env->FindClass("eu/lielu/arcextract/jni/NativeArcEntry");
    if (entryClass == nullptr) return nullptr;
    jmethodID ctor = env->GetMethodID(entryClass, "<init>", "(Ljava/lang/String;ZJJLjava/lang/String;J)V");
    if (ctor == nullptr) return nullptr;

    jobjectArray result = env->NewObjectArray(static_cast<jsize>(entries.size()), entryClass, nullptr);
    if (result == nullptr) return nullptr;

    for (size_t i = 0; i < entries.size(); ++i) {
        const ArcEntry& e = entries[i];
        jstring pathJ = env->NewStringUTF(e.path.c_str());
        jstring compressorJ = env->NewStringUTF(e.dataBlockCompressor.c_str());
        jobject obj = env->NewObject(
            entryClass, ctor,
            pathJ,
            static_cast<jboolean>(e.kind == EntryKind::Directory ? JNI_TRUE : JNI_FALSE),
            static_cast<jlong>(e.uncompressedSize),
            static_cast<jlong>(e.crc),
            compressorJ,
            static_cast<jlong>(e.dataBlockAbsolutePos));
        if (obj != nullptr) {
            env->SetObjectArrayElement(result, static_cast<jsize>(i), obj);
            env->DeleteLocalRef(obj);
        }
        env->DeleteLocalRef(pathJ);
        env->DeleteLocalRef(compressorJ);
    }
    return result;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_eu_lielu_arcextract_jni_ArcNative_readRawChunk(
    JNIEnv* env, jobject /*thiz*/, jint fd, jlong absolutePos, jint length) {
    try {
        FileSource source(static_cast<int>(fd));
        std::vector<uint8_t> buf = source.readAt(static_cast<uint64_t>(absolutePos), static_cast<uint64_t>(length));

        jbyteArray result = env->NewByteArray(static_cast<jsize>(buf.size()));
        if (result == nullptr) return nullptr;
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(buf.size()), reinterpret_cast<const jbyte*>(buf.data()));
        return result;
    } catch (const std::exception& e) {
        throwIOException(env, e.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_eu_lielu_arcextract_jni_ArcNative_isArcSignature(JNIEnv* env, jobject /*thiz*/, jbyteArray headBytes) {
    const jsize len = env->GetArrayLength(headBytes);
    if (len < 4) return JNI_FALSE;
    jbyte* bytes = env->GetByteArrayElements(headBytes, nullptr);
    const bool matches = static_cast<uint8_t>(bytes[0]) == kSignature[0] &&
                          static_cast<uint8_t>(bytes[1]) == kSignature[1] &&
                          static_cast<uint8_t>(bytes[2]) == kSignature[2] &&
                          static_cast<uint8_t>(bytes[3]) == kSignature[3];
    env->ReleaseByteArrayElements(headBytes, bytes, JNI_ABORT);
    return matches ? JNI_TRUE : JNI_FALSE;
}
