#pragma once
#include <jni.h>
#include <jvmti.h>
#include <string>
#include <functional>

class JVM
{
public:
    static JVM& instance();

    bool attach();
    void detach();

    // JNIEnv is per-thread: this returns the env of the CALLING thread, attaching it
    // (as a daemon thread) if it is a native thread such as the render thread.
    JNIEnv* env() const;
    JavaVM* vm()     const { return m_vm; }
    jvmtiEnv* jvmti() const { return m_jvmti; }

    // ── Minecraft accessors ──────────────────────────────────
    jobject getMinecraft();
    jobject getPlayer();
    jobject getWorld();
    jobject getNetworkHandler();

    // True if a GUI screen (chat, inventory, pause menu, ...) is open. Fails closed: returns
    // true when the answer cannot be determined.
    bool isScreenOpen();

    // ── Tick hook via JVMTI breakpoint ───────────────────────
    bool hookTick();
    void unhookTick();   // clears the breakpoint + callback; safe to call twice
    void setTickCallback(std::function<void()> cb) { m_tickCallback = std::move(cb); }

    // ── Class / method resolution ────────────────────────────
    jclass  findClass(const char* name);
    jmethodID getMethodID(jclass cls, const char* name, const char* sig);
    jmethodID getStaticMethodID(jclass cls, const char* name, const char* sig);
    jfieldID  getFieldID(jclass cls, const char* name, const char* sig);
    jfieldID  getStaticFieldID(jclass cls, const char* name, const char* sig);

    // ── Global ref helpers ───────────────────────────────────
    jobject makeGlobal(jobject local);
    void    releaseGlobal(jobject global);

private:
    JVM() = default;
    ~JVM();

    JavaVM*   m_vm   = nullptr;
    jvmtiEnv* m_jvmti = nullptr;
    bool      m_attached = false;

    // Cached
    jobject   m_mcGlobal     = nullptr;
    jclass    m_mcClass      = nullptr;
    jmethodID m_mcGetInstance = nullptr;

    // Tick
    std::function<void()> m_tickCallback;
    jmethodID m_tickMethod = nullptr;

    static void JNICALL breakpointCB(jvmtiEnv* jvmti, JNIEnv* jni,
        jthread thread, jmethodID method, jlocation location);
};
