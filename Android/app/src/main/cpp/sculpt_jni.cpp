// JNI bridge: one Kotlin `external fun` per function of AppCore's C API
// (AppCore/include/sculpt/sculpt_c.h). Documents are passed as jlong handles.
#include <jni.h>
#include <android/log.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "sculpt/sculpt_c.h"

namespace {

JavaVM* g_vm = nullptr;

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Sculpt3D", __VA_ARGS__)

SculptDoc* doc(jlong h) { return reinterpret_cast<SculptDoc*>(h); }

std::string str(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string out(c ? c : "");
  if (c) env->ReleaseStringUTFChars(s, c);
  return out;
}

// Takes ownership of a core string. Builds the Java string from UTF-8 bytes via
// String(byte[], UTF_8), since NewStringUTF expects modified UTF-8 and would
// mangle characters outside the BMP (emoji in names).
jstring take(JNIEnv* env, char* s) {
  if (!s) return nullptr;
  size_t n = strlen(s);
  jbyteArray bytes = env->NewByteArray(static_cast<jsize>(n));
  env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(n), reinterpret_cast<const jbyte*>(s));
  sculpt_free(s);
  jclass stringClass = env->FindClass("java/lang/String");
  jmethodID ctor = env->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");
  jstring charset = env->NewStringUTF("UTF-8");
  auto out = static_cast<jstring>(env->NewObject(stringClass, ctor, bytes, charset));
  env->DeleteLocalRef(bytes);
  env->DeleteLocalRef(charset);
  env->DeleteLocalRef(stringClass);
  return out;
}

jbyteArray takeBytes(JNIEnv* env, uint8_t* data, size_t n) {
  if (!data) return nullptr;
  jbyteArray out = env->NewByteArray(static_cast<jsize>(n));
  env->SetByteArrayRegion(out, 0, static_cast<jsize>(n), reinterpret_cast<const jbyte*>(data));
  sculpt_free(data);
  return out;
}

// Runs `runnable.run()` on the calling native thread, attaching it if needed.
void runRunnable(jobject runnable) {
  if (!g_vm || !runnable) return;
  JNIEnv* env = nullptr;
  bool attached = false;
  if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    attached = true;
  }
  jclass cls = env->GetObjectClass(runnable);
  jmethodID run = env->GetMethodID(cls, "run", "()V");
  env->CallVoidMethod(runnable, run);
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteLocalRef(cls);
  if (attached) g_vm->DetachCurrentThread();
}

std::mutex g_refsMutex;
std::unordered_map<SculptDoc*, jobject> g_listeners;  // global refs
jobject g_mcpPost = nullptr;
jobject g_newDocumentHandler = nullptr;

void listenerTrampoline(void* context) {
  jobject r = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_refsMutex);
    auto it = g_listeners.find(static_cast<SculptDoc*>(context));
    if (it != g_listeners.end()) r = it->second;
  }
  runRunnable(r);
}

void mcpPostTrampoline(void*) { runRunnable(g_mcpPost); }

SculptDoc* newDocumentTrampoline(void*, const char* name) {
  if (!g_vm || !g_newDocumentHandler) return nullptr;
  JNIEnv* env = nullptr;
  bool attached = false;
  if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    attached = true;
  }
  jclass cls = env->GetObjectClass(g_newDocumentHandler);
  jmethodID m = env->GetMethodID(cls, "newDocument", "(Ljava/lang/String;)J");
  jstring jname = env->NewStringUTF(name ? name : "Untitled");
  jlong h = env->CallLongMethod(g_newDocumentHandler, m, jname);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    h = 0;
  }
  env->DeleteLocalRef(jname);
  env->DeleteLocalRef(cls);
  if (attached) g_vm->DetachCurrentThread();
  return doc(h);
}

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  g_vm = vm;
  return JNI_VERSION_1_6;
}

#define FN(name) Java_com_higglerslab_sculpt3d_core_SculptNative_##name

extern "C" {

JNIEXPORT jstring JNICALL FN(version)(JNIEnv* env, jobject) { return env->NewStringUTF(sculpt_version()); }

// ---- Documents ----

JNIEXPORT jlong JNICALL FN(docCreate)(JNIEnv* env, jobject, jstring name) {
  return reinterpret_cast<jlong>(sculpt_doc_create(str(env, name).c_str()));
}

JNIEXPORT void JNICALL FN(docDestroy)(JNIEnv* env, jobject, jlong h) {
  {
    std::lock_guard<std::mutex> lock(g_refsMutex);
    auto it = g_listeners.find(doc(h));
    if (it != g_listeners.end()) {
      env->DeleteGlobalRef(it->second);
      g_listeners.erase(it);
    }
  }
  sculpt_doc_set_listener(doc(h), nullptr, nullptr);
  sculpt_doc_destroy(doc(h));
}

JNIEXPORT jstring JNICALL FN(docLoad)(JNIEnv* env, jobject, jlong h, jbyteArray data) {
  jsize n = env->GetArrayLength(data);
  jbyte* bytes = env->GetByteArrayElements(data, nullptr);
  char* err = sculpt_doc_load(doc(h), reinterpret_cast<const char*>(bytes), static_cast<size_t>(n));
  env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
  return take(env, err);
}

JNIEXPORT jbyteArray JNICALL FN(docSave)(JNIEnv* env, jobject, jlong h) {
  char* s = sculpt_doc_save(doc(h));
  return takeBytes(env, reinterpret_cast<uint8_t*>(s), s ? strlen(s) : 0);
}

JNIEXPORT jboolean JNICALL FN(docIsDirty)(JNIEnv*, jobject, jlong h) { return sculpt_doc_is_dirty(doc(h)) != 0; }
JNIEXPORT void JNICALL FN(docMarkSaved)(JNIEnv*, jobject, jlong h) { sculpt_doc_mark_saved(doc(h)); }
JNIEXPORT void JNICALL FN(docSetName)(JNIEnv* env, jobject, jlong h, jstring name) {
  sculpt_doc_set_name(doc(h), str(env, name).c_str());
}

// ---- State and commands ----

JNIEXPORT jlong JNICALL FN(docRevision)(JNIEnv*, jobject, jlong h) {
  return static_cast<jlong>(sculpt_doc_revision(doc(h)));
}

JNIEXPORT void JNICALL FN(docSetListener)(JNIEnv* env, jobject, jlong h, jobject runnable) {
  std::lock_guard<std::mutex> lock(g_refsMutex);
  auto it = g_listeners.find(doc(h));
  if (it != g_listeners.end()) {
    env->DeleteGlobalRef(it->second);
    g_listeners.erase(it);
  }
  if (runnable) {
    g_listeners[doc(h)] = env->NewGlobalRef(runnable);
    sculpt_doc_set_listener(doc(h), listenerTrampoline, doc(h));
  } else {
    sculpt_doc_set_listener(doc(h), nullptr, nullptr);
  }
}

JNIEXPORT jstring JNICALL FN(docState)(JNIEnv* env, jobject, jlong h) { return take(env, sculpt_doc_state(doc(h))); }

JNIEXPORT jstring JNICALL FN(docCommand)(JNIEnv* env, jobject, jlong h, jstring json) {
  return take(env, sculpt_doc_command(doc(h), str(env, json).c_str()));
}

// ---- Files ----

// Returns the exported bytes, or throws java.io.IOException with the message.
JNIEXPORT jbyteArray JNICALL FN(docExport)(JNIEnv* env, jobject, jlong h, jstring format, jstring options) {
  size_t n = 0;
  char* err = nullptr;
  std::string opts = str(env, options);
  uint8_t* data = sculpt_doc_export(doc(h), str(env, format).c_str(), opts.empty() ? nullptr : opts.c_str(), &n, &err);
  if (!data) {
    std::string msg = err ? err : "Export failed";
    if (err) sculpt_free(err);
    env->ThrowNew(env->FindClass("java/io/IOException"), msg.c_str());
    return nullptr;
  }
  return takeBytes(env, data, n);
}

JNIEXPORT jstring JNICALL FN(docImportPath)(JNIEnv* env, jobject, jlong h, jstring path) {
  return take(env, sculpt_doc_import_path(doc(h), str(env, path).c_str()));
}

JNIEXPORT jstring JNICALL FN(docImportImage)(JNIEnv* env, jobject, jlong h, jbyteArray data, jstring name,
                                             jboolean decal) {
  jsize n = env->GetArrayLength(data);
  jbyte* bytes = env->GetByteArrayElements(data, nullptr);
  char* err = sculpt_doc_import_image(doc(h), reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(n),
                                      str(env, name).c_str(), decal ? 1 : 0);
  env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
  return take(env, err);
}

JNIEXPORT jstring JNICALL FN(docImportImageAs)(JNIEnv* env, jobject, jlong h, jbyteArray data, jstring name, jint mode) {
  jsize n = env->GetArrayLength(data);
  jbyte* bytes = env->GetByteArrayElements(data, nullptr);
  char* err = sculpt_doc_import_image(doc(h), reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(n),
                                      str(env, name).c_str(), mode);
  env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
  return take(env, err);
}

// ---- Viewport (GL thread) ----

JNIEXPORT void JNICALL FN(viewGlInit)(JNIEnv*, jobject, jlong h) { sculpt_view_gl_init(doc(h)); }
JNIEXPORT void JNICALL FN(viewGlRelease)(JNIEnv*, jobject, jlong h) { sculpt_view_gl_release(doc(h)); }
JNIEXPORT void JNICALL FN(viewResize)(JNIEnv*, jobject, jlong h, jint w, jint hh, jfloat scale) {
  sculpt_view_resize(doc(h), w, hh, scale);
}
JNIEXPORT void JNICALL FN(viewRender)(JNIEnv*, jobject, jlong h) { sculpt_view_render(doc(h)); }
JNIEXPORT jboolean JNICALL FN(viewNeedsRedraw)(JNIEnv*, jobject, jlong h) {
  return sculpt_view_needs_redraw(doc(h)) != 0;
}
JNIEXPORT jbyteArray JNICALL FN(viewSnapshot)(JNIEnv* env, jobject, jlong h, jint w, jint hh) {
  size_t n = 0;
  return takeBytes(env, sculpt_view_snapshot(doc(h), w, hh, &n), n);
}
JNIEXPORT jbyteArray JNICALL FN(viewSnapshotPng)(JNIEnv* env, jobject, jlong h, jint w, jint hh) {
  size_t n = 0;
  return takeBytes(env, sculpt_view_snapshot_png(doc(h), w, hh, &n), n);
}
JNIEXPORT jlong JNICALL FN(viewRevision)(JNIEnv*, jobject, jlong h) {
  return static_cast<jlong>(sculpt_view_revision(doc(h)));
}
JNIEXPORT jstring JNICALL FN(viewAnnotations)(JNIEnv* env, jobject, jlong h) {
  return take(env, sculpt_view_annotations(doc(h)));
}

// ---- Viewport input ----

JNIEXPORT void JNICALL FN(viewPointer)(JNIEnv*, jobject, jlong h, jint id, jint type, jint button, jint phase,
                                       jfloat x, jfloat y, jint mods) {
  sculpt_view_pointer(doc(h), id, type, button, phase, x, y, mods);
}
JNIEXPORT void JNICALL FN(viewOrbit)(JNIEnv*, jobject, jlong h, jfloat dx, jfloat dy) {
  sculpt_view_orbit(doc(h), dx, dy);
}
JNIEXPORT void JNICALL FN(viewPan)(JNIEnv*, jobject, jlong h, jfloat dx, jfloat dy) { sculpt_view_pan(doc(h), dx, dy); }
JNIEXPORT void JNICALL FN(viewZoom)(JNIEnv*, jobject, jlong h, jfloat f, jfloat x, jfloat y) {
  sculpt_view_zoom(doc(h), f, x, y);
}
JNIEXPORT jboolean JNICALL FN(viewKey)(JNIEnv* env, jobject, jlong h, jstring key, jint mods) {
  return sculpt_view_key(doc(h), str(env, key).c_str(), mods) != 0;
}

// ---- MCP ----

JNIEXPORT jstring JNICALL FN(mcpStart)(JNIEnv* env, jobject, jint port, jobject post) {
  {
    std::lock_guard<std::mutex> lock(g_refsMutex);
    if (g_mcpPost) env->DeleteGlobalRef(g_mcpPost);
    g_mcpPost = post ? env->NewGlobalRef(post) : nullptr;
  }
  return take(env, sculpt_mcp_start(port, mcpPostTrampoline, nullptr));
}
JNIEXPORT void JNICALL FN(mcpStop)(JNIEnv*, jobject) { sculpt_mcp_stop(); }
JNIEXPORT jboolean JNICALL FN(mcpRunning)(JNIEnv*, jobject) { return sculpt_mcp_running() != 0; }
JNIEXPORT void JNICALL FN(mcpDrain)(JNIEnv*, jobject) { sculpt_mcp_drain(); }
JNIEXPORT void JNICALL FN(mcpRegister)(JNIEnv*, jobject, jlong h) { sculpt_mcp_register(doc(h)); }
JNIEXPORT void JNICALL FN(mcpUnregister)(JNIEnv*, jobject, jlong h) { sculpt_mcp_unregister(doc(h)); }
JNIEXPORT void JNICALL FN(mcpActivate)(JNIEnv*, jobject, jlong h) { sculpt_mcp_activate(doc(h)); }
JNIEXPORT void JNICALL FN(mcpSetNewDocumentHandler)(JNIEnv* env, jobject, jobject handler) {
  {
    std::lock_guard<std::mutex> lock(g_refsMutex);
    if (g_newDocumentHandler) env->DeleteGlobalRef(g_newDocumentHandler);
    g_newDocumentHandler = handler ? env->NewGlobalRef(handler) : nullptr;
  }
  sculpt_mcp_set_new_document_handler(handler ? newDocumentTrampoline : nullptr, nullptr);
}

}  // extern "C"
