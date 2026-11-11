/**
 * @file jni_bridge.cpp
 * @brief JNI entry points exposing DasherCore to the Kotlin layer.
 *
 * Each @c Java_com_janmurin_dashermobile_NativeBridge_* function corresponds directly to an
 * @c external fun declaration in @c NativeBridge.kt.
 *
 * ## Session model
 * A @c NativeSession is heap-allocated by @c nativeCreate and identified by a @c jlong
 * "handle" that is just the pointer cast to an integer.  All subsequent calls pass this
 * handle back so the bridge can look up the session without any global state.
 *
 * ## Draw-command protocol
 * @c nativeFrame returns a @c jintArray that is a flat sequence of 6-integer records:
 * @code
 *   [ op, a, b, c, d, argb ]
 * @endcode
 * See @c AndroidDasherInterface.h for the full opcode table.
 * If the frame produced by DasherCore contains no visible boxes, fallback placeholder boxes
 * are appended by @ref appendFallbackBoxes so the canvas is never completely empty during
 * startup or while the engine is paused.
 */
#include "AndroidDasherInterface.h"
#include "AndroidFiles.h"
#include "LanguageSet.h"

#include <jni.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#define LOG_TAG "DasherJNI"

namespace {

/**
 * @brief All mutable state for a single Dasher session.
 *
 * One instance is created per @c nativeCreate call.  Its address is cast to a @c jlong
 * and passed back to Java as an opaque handle.
 */
struct NativeSession {
    std::unique_ptr<AndroidDasherInterface> iface; ///< The Dasher engine.
    int width = 0;                ///< Last known surface width.
    int height = 0;               ///< Last known surface height.
    float touchX = 0.0f;         ///< Last known touch X (used for fallback rendering).
    float touchY = 0.0f;         ///< Last known touch Y (used for fallback rendering).
    bool touchActive = false;     ///< @c true while a finger is on screen.

    NativeSession(const std::string &filesDir, const std::string &languages)
        : iface(std::make_unique<AndroidDasherInterface>(filesDir, languages)) {}
};

/// @brief Copies a Java string; empty if it is null.
static std::string toStdString(JNIEnv *env, jstring text) {
    if (!text) return {};
    const char *chars = env->GetStringUTFChars(text, nullptr);
    if (!chars) return {};
    std::string copy(chars);
    env->ReleaseStringUTFChars(text, chars);
    return copy;
}

/**
 * @brief Converts a @c jlong handle back to a @c NativeSession pointer.
 * @param handle Value previously returned by @ref toHandle.
 * @return Pointer to the session, or @c nullptr if @p handle is 0.
 */
static inline NativeSession *fromHandle(jlong handle) {
    return reinterpret_cast<NativeSession *>(static_cast<uintptr_t>(handle));
}

/**
 * @brief Converts a @c NativeSession pointer to a @c jlong handle.
 * @param session Non-null pointer to a heap-allocated @c NativeSession.
 * @return Opaque integer handle suitable for storing in Kotlin's @c Long.
 */
static inline jlong toHandle(NativeSession *session) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(session));
}

/**
 * @brief Creates a @c jintArray from a @c std::vector<jint>.
 * @param env  JNI environment.
 * @param data Source data; may be empty.
 * @return Newly allocated @c jintArray; never @c nullptr (may have zero length).
 */
static jintArray toJIntArray(JNIEnv *env, const std::vector<jint> &data) {
    jintArray result = env->NewIntArray(static_cast<jsize>(data.size()));
    if (!result || data.empty()) {
        return result;
    }
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(data.size()), data.data());
    return result;
}

/**
 * @brief Returns @c true when @p commands contains at least one visible filled or
 *        stroked rectangle (op == 3 or 4) with non-transparent colour and non-zero area.
 *
 * Used to detect frames where DasherCore produced meaningful content so the fallback
 * placeholder boxes are not emitted unnecessarily.
 *
 * @param commands Flat draw-command buffer.
 */
static bool hasVisibleBoxCommands(const std::vector<jint> &commands) {
    for (size_t i = 0; i + 5 < commands.size(); i += 6) {
        const int op = commands[i];
        if (op == 3 || op == 4) {
            const int a = commands[i + 1];
            const int b = commands[i + 2];
            const int c = commands[i + 3];
            const int d = commands[i + 4];
            const int color = commands[i + 5];
            const int alpha = (color >> 24) & 0xFF;
            if (alpha < 24) {
                continue;
            }
            if (a == c || b == d) {
                continue;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief Appends a single 6-element draw-command record to @p commands.
 *
 * Convenience wrapper used by @ref appendFallbackBoxes.
 *
 * @param commands Destination buffer.
 * @param op       Primitive opcode.
 * @param a–d      Primitive parameters.
 * @param color    Packed ARGB colour.
 */
static void pushCommand(std::vector<jint> &commands,
                        jint op,
                        jint a,
                        jint b,
                        jint c,
                        jint d,
                        jint color) {
    commands.push_back(op);
    commands.push_back(a);
    commands.push_back(b);
    commands.push_back(c);
    commands.push_back(d);
    commands.push_back(color);
}

/**
 * @brief Appends placeholder Dasher boxes to @p commands when the real frame is empty.
 *
 * Draws two large coloured rectangles (green upper half, blue lower half) and a crosshair
 * at the current or centred touch position.  This ensures the canvas is never completely
 * blank during engine startup, orientation changes, or loading delays.
 *
 * @param session  Session whose surface size and touch state are used for positioning.
 * @param commands Draw-command buffer to append to.
 */
static void appendFallbackBoxes(NativeSession &session, std::vector<jint> &commands) {
    if (session.width <= 0 || session.height <= 0) {
        return;
    }

    const int w = session.width;
    const int h = session.height;
    const int midY = h / 2;
    const int left = w / 20;
    const int right = w - left;

    pushCommand(commands, 4, left, h / 8, right, midY - h / 20, static_cast<jint>(0xFF1B5E20));
    pushCommand(commands, 3, left, h / 8, right, midY - h / 20, static_cast<jint>(0xFF81C784));

    pushCommand(commands, 4, left, midY + h / 20, right, h - h / 8, static_cast<jint>(0xFF0D47A1));
    pushCommand(commands, 3, left, midY + h / 20, right, h - h / 8, static_cast<jint>(0xFF90CAF9));

    const int px = session.touchActive ? static_cast<int>(session.touchX) : (w / 2);
    const int py = session.touchActive ? static_cast<int>(session.touchY) : (h / 2);
    const int marker = std::max(10, std::min(w, h) / 45);

    pushCommand(commands, 1, px, py, marker, 1, static_cast<jint>(0xFFD50000));
    pushCommand(commands, 2, px - marker * 2, py, px + marker * 2, py, static_cast<jint>(0xFFFFCDD2));
    pushCommand(commands, 2, px, py - marker * 2, px, py + marker * 2, static_cast<jint>(0xFFFFCDD2));
}

/**
 * @brief Periodically logs frame statistics to the Android logcat.
 *
 * Runs only once every 120 frames to avoid log spam.  Prints the total command count
 * and the bounding box that encompasses all drawn primitives, plus a brief preview of
 * the first few commands.
 *
 * @param commands Draw-command buffer for the current frame.
 */
static void logFrameDiagnostics(const std::vector<jint> &commands) {
    static int frameCounter = 0;
    frameCounter++;
    if (frameCounter % 120 != 0) return;

    if (commands.empty()) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "frame commands empty");
        return;
    }

    int minX = INT32_MAX;
    int minY = INT32_MAX;
    int maxX = INT32_MIN;
    int maxY = INT32_MIN;

    const int total = static_cast<int>(commands.size() / 6);
    const int preview = std::min(total, 4);
    for (int i = 0; i < total; ++i) {
        const int base = i * 6;
        const int op = commands[base];
        const int a = commands[base + 1];
        const int b = commands[base + 2];
        const int c = commands[base + 3];
        const int d = commands[base + 4];
        if (op == 1) {
            minX = std::min(minX, a - c);
            maxX = std::max(maxX, a + c);
            minY = std::min(minY, b - c);
            maxY = std::max(maxY, b + c);
        } else if (op == 2 || op == 3 || op == 4) {
            minX = std::min(minX, std::min(a, c));
            maxX = std::max(maxX, std::max(a, c));
            minY = std::min(minY, std::min(b, d));
            maxY = std::max(maxY, std::max(b, d));
        }
        if (i < preview) {
            __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG,
                                "op[%d]=%d a=%d b=%d c=%d d=%d",
                                i, op, a, b, c, d);
        }
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "cmds=%d bounds=[%d,%d]-[%d,%d]",
                        total, minX, minY, maxX, maxY);
}

}

extern "C" {

/** @brief JNI binding for @c NativeBridge.nativeVersion. Returns the DasherCore version string. */
JNIEXPORT jstring JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeVersion(JNIEnv *env, jclass) {
    return env->NewStringUTF("DasherCore 1.0");
}

/**
 * @brief JNI binding for @c NativeBridge.nativeCreate.
 *
 * Allocates a @c NativeSession on the heap and registers @p filesDir as the user-data
 * directory with the Android file-utils layer.
 *
 * @param filesDir  UTF-8 path to the app's private files directory.
 * @param languages Languages to predict for, e.g. "de+en" (see LanguageSet.h).
 * @return Opaque handle (non-zero on success, 0 on failure).
 */
JNIEXPORT jlong JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeCreate(JNIEnv *env, jclass, jstring filesDir, jstring languages) {
    const std::string dirStr = toStdString(env, filesDir);
    if (dirStr.empty()) {
        return 0L;
    }

    DasherAndroid::SetUserDataDir(dirStr);
    auto *session = new NativeSession(dirStr, toStdString(env, languages));
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "nativeCreate handle=%p", static_cast<void *>(session));
    return toHandle(session);
}

/**
 * @brief JNI binding for @c NativeBridge.nativeDestroy.
 *
 * Deletes the @c NativeSession.  The handle must not be used after this call.
 *
 * @param handle Opaque session handle.
 */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeDestroy(JNIEnv *, jclass, jlong handle) {
    auto *session = fromHandle(handle);
    delete session;
}

/**
 * @brief JNI binding for @c NativeBridge.nativeSetAssetManager.
 *
 * Passes the Android @c AAssetManager to the file-utils layer so DasherCore can open
 * alphabet XML files from the APK's asset tree.
 *
 * @param handle       Opaque session handle.
 * @param assetManager Java @c AssetManager object.
 */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSetAssetManager(JNIEnv *env,
                                                                    jclass,
                                                                    jlong handle,
                                                                    jobject assetManager) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface || !assetManager) {
        return;
    }
    AAssetManager *nativeAssetManager = AAssetManager_fromJava(env, assetManager);
    if (!nativeAssetManager) {
        return;
    }
    DasherAndroid::SetAssetManager(nativeAssetManager);
}

/**
 * @brief JNI binding for @c NativeBridge.nativeSetScreenSize.
 *
 * Caches the surface dimensions in the session (used by the fallback renderer) and
 * forwards them to the Dasher engine.
 *
 * @param handle Opaque session handle.
 * @param width  Surface width in pixels.
 * @param height Surface height in pixels.
 */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSetScreenSize(JNIEnv *,
                                                                  jclass,
                                                                  jlong handle,
                                                                  jint width,
                                                                  jint height) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return;
    }
    session->width = std::max(0, static_cast<int>(width));
    session->height = std::max(0, static_cast<int>(height));
    session->iface->SetScreenSize(static_cast<int>(width), static_cast<int>(height));
}

/**
 * @brief JNI binding for @c NativeBridge.nativeTouch.
 *
 * Caches the touch position and state in the session (used by the fallback renderer) then
 * forwards the event to the Dasher engine.
 *
 * @param handle Opaque session handle.
 * @param action 0 = DOWN, 1 = MOVE, 2 = UP/CANCEL.
 * @param x      Touch X in surface pixels.
 * @param y      Touch Y in surface pixels.
 */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeTouch(JNIEnv *,
                                                          jclass,
                                                          jlong handle,
                                                          jint action,
                                                          jfloat x,
                                                          jfloat y) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return;
    }
    session->touchX = static_cast<float>(x);
    session->touchY = static_cast<float>(y);
    if (action == 0) {
        session->touchActive = true;
    } else if (action == 2) {
        session->touchActive = false;
    }
    session->iface->SetTouch(static_cast<int>(action), static_cast<float>(x), static_cast<float>(y));
}

/** @brief JNI binding for @c NativeBridge.nativeGetLanguages. Returns the session's language set. */
JNIEXPORT jstring JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeGetLanguages(JNIEnv *env,
                                                                 jclass,
                                                                 jlong handle) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return env->NewStringUTF("");
    }
    return env->NewStringUTF(session->iface->GetLanguages().c_str());
}

/** @brief JNI binding for @c NativeBridge.nativeGetLanguageModelId. */
JNIEXPORT jint JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeGetLanguageModelId(JNIEnv *,
                                                                       jclass,
                                                                       jlong handle) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return 0;
    }
    return static_cast<jint>(session->iface->GetLanguageModelId());
}

/** @brief JNI binding for @c NativeBridge.nativeSetLanguageModelId. */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSetLanguageModelId(JNIEnv *,
                                                                       jclass,
                                                                       jlong handle,
                                                                       jint modelId) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return;
    }
    session->iface->SetLanguageModelId(static_cast<int>(modelId));
}

/** @brief JNI binding for @c NativeBridge.nativeGetMovementSpeedPercent. */
JNIEXPORT jint JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeGetMovementSpeedPercent(JNIEnv *,
                                                                            jclass,
                                                                            jlong handle) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return 100;
    }
    return static_cast<jint>(session->iface->GetMovementSpeedPercent());
}

/** @brief JNI binding for @c NativeBridge.nativeSetMovementSpeedPercent. */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSetMovementSpeedPercent(JNIEnv *,
                                                                            jclass,
                                                                            jlong handle,
                                                                            jint percent) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return;
    }
    session->iface->SetMovementSpeedPercent(static_cast<int>(percent));
}

/**
 * @brief JNI binding for @c NativeBridge.nativeFrame.
 *
 * Advances the Dasher model by one step and returns the resulting draw commands.
 * If the engine produces no visible box commands (e.g. during startup), placeholder
 * fallback boxes are appended so the canvas is never blank.
 *
 * @param handle  Opaque session handle.
 * @param timeMs  Current frame timestamp in milliseconds.
 * @return @c jintArray of flat 6-integer draw-command records; empty on error.
 */
JNIEXPORT jintArray JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeFrame(JNIEnv *env,
                                                         jclass,
                                                         jlong handle,
                                                         jlong timeMs) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return env->NewIntArray(0);
    }

    const auto rawCommands = session->iface->Frame(static_cast<long>(timeMs));
    std::vector<jint> commands(rawCommands.begin(), rawCommands.end());
    if (!hasVisibleBoxCommands(commands)) {
        appendFallbackBoxes(*session, commands);
    }
    logFrameDiagnostics(commands);
    return toJIntArray(env, commands);
}

/**
 * @brief JNI binding for @c NativeBridge.nativeGetFrameStrings.
 *
 * Returns and clears the string labels queued during the most recent frame.
 * Each string corresponds to an @c op=5 draw command whose @c d field is the index
 * into this array.
 *
 * @param handle Opaque session handle.
 * @return @c jobjectArray of @c java.lang.String; empty array on error.
 */
JNIEXPORT jobjectArray JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeGetFrameStrings(JNIEnv *env,
                                                                    jclass,
                                                                    jlong handle) {
    jclass strClass = env->FindClass("java/lang/String");
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return env->NewObjectArray(0, strClass, nullptr);
    }
    const auto strings = session->iface->TakeFrameStrings();

    static int s_logCounter = 0;
    if (++s_logCounter % 120 == 0) {
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "frameStrings count=%zu", strings.size());
    }

    auto arr = env->NewObjectArray(static_cast<jsize>(strings.size()), strClass, nullptr);
    for (jsize i = 0; i < static_cast<jsize>(strings.size()); ++i) {
        jstring jstr = env->NewStringUTF(strings[i].c_str());
        env->SetObjectArrayElement(arr, i, jstr);
        env->DeleteLocalRef(jstr);
    }
    return arr;
}

/** @brief JNI binding for @c NativeBridge.nativeGetOutputText. Returns the accumulated output text. */
JNIEXPORT jstring JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeGetOutputText(JNIEnv *env,
                                                                  jclass,
                                                                  jlong handle) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return env->NewStringUTF("");
    }
    return env->NewStringUTF(session->iface->GetOutputText().c_str());
}

/** @brief JNI binding for @c NativeBridge.nativeResetOutputText. Clears the output text buffer. */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeResetOutputText(JNIEnv *,
                                                                    jclass,
                                                                    jlong handle) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return;
    }
    session->iface->ResetOutputText();
}

/** @brief JNI binding for @c NativeBridge.nativeFlushTrainingText. Saves learned text to the user training file. */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeFlushTrainingText(JNIEnv *,
                                                                      jclass,
                                                                      jlong handle) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return;
    }
    session->iface->FlushTrainingText();
}

/**
 * @brief JNI binding for @c NativeBridge.nativeSetContext.
 *
 * Loads the text before an editor's cursor as if it had been written with Dasher.
 *
 * @return The plain text actually loaded (the new output text).
 */
JNIEXPORT jstring JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSetContext(JNIEnv *env,
                                                               jclass,
                                                               jlong handle,
                                                               jstring textBeforeCursor) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface || !textBeforeCursor) {
        return env->NewStringUTF("");
    }
    const char *chars = env->GetStringUTFChars(textBeforeCursor, nullptr);
    if (!chars) {
        return env->NewStringUTF("");
    }
    const std::string text(chars);
    env->ReleaseStringUTFChars(textBeforeCursor, chars);
    return env->NewStringUTF(session->iface->SetContext(text).c_str());
}

/** @brief JNI binding for @c NativeBridge.nativeSetLearningEnabled. Turns learning from written text on or off. */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSetLearningEnabled(JNIEnv *,
                                                                       jclass,
                                                                       jlong handle,
                                                                       jboolean enabled) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return;
    }
    session->iface->SetLearningEnabled(enabled == JNI_TRUE);
}

/** @brief JNI binding for @c NativeBridge.nativeAlphabetIdFor. Names the alphabet of a language set. */
JNIEXPORT jstring JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeAlphabetIdFor(JNIEnv *env, jclass, jstring languages) {
    const auto parsed = DasherAndroid::ParseLanguages(toStdString(env, languages));
    return env->NewStringUTF(DasherAndroid::AlphabetIdFor(parsed).c_str());
}

/** @brief JNI binding for @c NativeBridge.nativeCorpusAssetFor. Names a language's bundled training text. */
JNIEXPORT jstring JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeCorpusAssetFor(JNIEnv *env, jclass, jstring language) {
    return env->NewStringUTF(DasherAndroid::CorpusAssetFor(toStdString(env, language)).c_str());
}

/** @brief JNI binding for @c NativeBridge.nativeImportedCorpusFileFor. Names the file a training text is imported to. */
JNIEXPORT jstring JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeImportedCorpusFileFor(JNIEnv *env, jclass, jstring language) {
    return env->NewStringUTF(DasherAndroid::ImportedCorpusFileFor(toStdString(env, language)).c_str());
}

/** @brief JNI binding for @c NativeBridge.nativeSteerToSymbol. Starts moving into a symbol's box. */
JNIEXPORT jboolean JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSteerToSymbol(JNIEnv *env,
                                                                  jclass,
                                                                  jlong handle,
                                                                  jstring text) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return JNI_FALSE;
    }
    return session->iface->SteerToSymbol(toStdString(env, text)) ? JNI_TRUE : JNI_FALSE;
}

/** @brief JNI binding for @c NativeBridge.nativeSteerBack. Starts moving out of the last symbol's box. */
JNIEXPORT jboolean JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeSteerBack(JNIEnv *, jclass, jlong handle) {
    auto *session = fromHandle(handle);
    if (!session || !session->iface) {
        return JNI_FALSE;
    }
    return session->iface->SteerBack() ? JNI_TRUE : JNI_FALSE;
}

/** @brief JNI binding for @c NativeBridge.nativeIsSteering. */
JNIEXPORT jboolean JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeIsSteering(JNIEnv *, jclass, jlong handle) {
    auto *session = fromHandle(handle);
    return session && session->iface && session->iface->IsSteering() ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief JNI binding for @c NativeBridge.nativeDictionaryEntries.
 *
 * @return One string per personal dictionary entry, the most often written first:
 *         count, "1" if added by hand else "0", language set and text, separated by tabs.
 */
JNIEXPORT jobjectArray JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeDictionaryEntries(JNIEnv *env, jclass, jstring filesDir) {
    DasherAndroid::SetUserDataDir(toStdString(env, filesDir));
    const auto entries = DasherAndroid::Dictionary().Entries();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(entries.size()), stringClass, nullptr);
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        const std::string line = std::to_string(entry.count) + "\t" + (entry.manual ? "1" : "0") + "\t"
            + entry.languages + "\t" + entry.text;
        jstring item = env->NewStringUTF(line.c_str());
        env->SetObjectArrayElement(result, static_cast<jsize>(i), item);
        env->DeleteLocalRef(item);
    }
    return result;
}

/** @brief JNI binding for @c NativeBridge.nativeDictionaryAdd. Adds an entry by hand. */
JNIEXPORT jboolean JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeDictionaryAdd(JNIEnv *env,
                                                                  jclass,
                                                                  jstring filesDir,
                                                                  jstring text,
                                                                  jstring languages) {
    DasherAndroid::SetUserDataDir(toStdString(env, filesDir));
    auto &dictionary = DasherAndroid::Dictionary();
    const bool added = dictionary.Add(toStdString(env, text), toStdString(env, languages),
                                      static_cast<long long>(std::time(nullptr)));
    return added && dictionary.Save() ? JNI_TRUE : JNI_FALSE;
}

/** @brief JNI binding for @c NativeBridge.nativeDictionaryRemove. Removes an entry. */
JNIEXPORT jboolean JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeDictionaryRemove(JNIEnv *env,
                                                                     jclass,
                                                                     jstring filesDir,
                                                                     jstring text,
                                                                     jstring languages) {
    DasherAndroid::SetUserDataDir(toStdString(env, filesDir));
    auto &dictionary = DasherAndroid::Dictionary();
    const bool removed = dictionary.Remove(toStdString(env, text), toStdString(env, languages));
    return removed && dictionary.Save() ? JNI_TRUE : JNI_FALSE;
}

/** @brief JNI binding for @c NativeBridge.nativeDictionaryLearn. Counts the words of dictated text. */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeDictionaryLearn(JNIEnv *env,
                                                                    jclass,
                                                                    jstring filesDir,
                                                                    jstring text,
                                                                    jstring languages) {
    DasherAndroid::SetUserDataDir(toStdString(env, filesDir));
    auto &dictionary = DasherAndroid::Dictionary();
    const std::string set = DasherAndroid::JoinLanguages(DasherAndroid::ParseLanguages(toStdString(env, languages)));
    dictionary.Learn(toStdString(env, text), set, static_cast<long long>(std::time(nullptr)));
    dictionary.Save();
}

/** @brief JNI binding for @c NativeBridge.nativeForgetLearned. Deletes everything learned from the user. */
JNIEXPORT void JNICALL
Java_com_janmurin_dashermobile_NativeBridge_nativeForgetLearned(JNIEnv *env, jclass, jstring filesDir) {
    DasherAndroid::SetUserDataDir(toStdString(env, filesDir));
    DasherAndroid::ForgetLearnedText();
}

}
