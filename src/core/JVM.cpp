#include "JVM.h"
#include "Mappings.h"
#include "../utils/Logger.h"

#include <Windows.h>

typedef jint(WINAPI* Fn_GetCreatedJavaVMs)(JavaVM**, jsize, jsize*);

namespace
{
    // True only on threads that THIS client attached to the JVM (the init thread, or a native
    // thread that called env()). Threads created by the JVM itself -- the game's render and tick
    // threads -- are never marked, so they are never detached by us.
    thread_local bool t_attachedByUs = false;
}

JVM& JVM::instance()
{
    static JVM inst;
    return inst;
}

bool JVM::attach()
{
    HMODULE hJvm = GetModuleHandleA("jvm.dll");
    if (!hJvm)
        hJvm = LoadLibraryA("jvm.dll");
    if (!hJvm)
    {
        LOG_ERROR("jvm.dll not found");
        return false;
    }

    auto pfn = (Fn_GetCreatedJavaVMs)GetProcAddress(hJvm, "JNI_GetCreatedJavaVMs");
    if (!pfn)
    {
        LOG_ERROR("JNI_GetCreatedJavaVMs not found");
        return false;
    }

    jsize count = 0;
    if (pfn(&m_vm, 1, &count) != JNI_OK || count == 0)
    {
        LOG_ERROR("No running JVM found");
        return false;
    }
    LOG_INFO("Found JVM");

    // The init thread is a plain native thread: attach it (daemon, so it never blocks JVM exit).
    {
        JNIEnv* probe = nullptr;
        const jint r = m_vm->GetEnv((void**)&probe, JNI_VERSION_21);
        if (r == JNI_EVERSION)
        {
            LOG_ERROR("The running JVM does not support JNI 21 (need Java 21 or newer)");
            return false;
        }
        if (r != JNI_OK)
        {
            if (m_vm->AttachCurrentThreadAsDaemon((void**)&probe, nullptr) != JNI_OK)
            {
                LOG_ERROR("AttachCurrentThread failed");
                return false;
            }
            t_attachedByUs = true;
        }
    }
    LOG_INFO("Attached to JVM");

    // Try JVMTI
    if (m_vm->GetEnv((void**)&m_jvmti, JVMTI_VERSION_1_0) == JNI_OK)
    {
        jvmtiCapabilities caps = {};
        caps.can_generate_breakpoint_events = 1;
        caps.can_access_local_variables     = 1;
        caps.can_get_bytecodes              = 1;
        m_jvmti->AddCapabilities(&caps);
        LOG_INFO("JVMTI enabled");
    }
    else
    {
        LOG_WARN("JVMTI not available; tick hook will use frame fallback");
    }

    return true;
}

JNIEnv* JVM::env() const
{
    if (!m_vm) return nullptr;
    JNIEnv* e = nullptr;
    const jint r = m_vm->GetEnv((void**)&e, JNI_VERSION_21);
    if (r == JNI_OK) return e;
    if (r == JNI_EDETACHED && m_vm->AttachCurrentThreadAsDaemon((void**)&e, nullptr) == JNI_OK)
    {
        t_attachedByUs = true;
        return e;
    }
    return nullptr;
}

void JVM::detach()
{
    // Detach the CALLING thread, and only if this client attached it.
    if (t_attachedByUs && m_vm)
    {
        m_vm->DetachCurrentThread();
        t_attachedByUs = false;
    }
    m_attached = false;
}

JVM::~JVM()
{
    if (m_jvmti)
        m_jvmti->DisposeEnvironment();
    detach();
}

// ── Class / method resolution ────────────────────────────────

jclass JVM::findClass(const char* name)
{
    JNIEnv* e = env();
    if (!e) return nullptr;
    jclass local = e->FindClass(name);
    if (!local)
    {
        e->ExceptionClear();
        return nullptr;
    }
    // Return local ref; caller should make global if persisting
    return local;
}

jmethodID JVM::getMethodID(jclass cls, const char* name, const char* sig)
{
    JNIEnv* e = env();
    if (!e || !cls) return nullptr;
    jmethodID mid = e->GetMethodID(cls, name, sig);
    if (!mid) e->ExceptionClear();
    return mid;
}

jmethodID JVM::getStaticMethodID(jclass cls, const char* name, const char* sig)
{
    JNIEnv* e = env();
    if (!e || !cls) return nullptr;
    jmethodID mid = e->GetStaticMethodID(cls, name, sig);
    if (!mid) e->ExceptionClear();
    return mid;
}

jfieldID JVM::getFieldID(jclass cls, const char* name, const char* sig)
{
    JNIEnv* e = env();
    if (!e || !cls) return nullptr;
    jfieldID fid = e->GetFieldID(cls, name, sig);
    if (!fid) e->ExceptionClear();
    return fid;
}

jfieldID JVM::getStaticFieldID(jclass cls, const char* name, const char* sig)
{
    JNIEnv* e = env();
    if (!e || !cls) return nullptr;
    jfieldID fid = e->GetStaticFieldID(cls, name, sig);
    if (!fid) e->ExceptionClear();
    return fid;
}

jobject JVM::makeGlobal(jobject local)
{
    JNIEnv* e = env();
    if (!e || !local) return nullptr;
    return e->NewGlobalRef(local);
}

void JVM::releaseGlobal(jobject global)
{
    JNIEnv* e = env();
    if (!e || !global) return;
    e->DeleteGlobalRef(global);
}

// ── Minecraft accessors ──────────────────────────────────────

jobject JVM::getMinecraft()
{
    if (m_mcGlobal) return m_mcGlobal;

    m_mcClass = findClass(Mappings::Cls::MinecraftClient);
    if (!m_mcClass)
    {
        LOG_ERROR("MinecraftClient class not found");
        return nullptr;
    }
    m_mcClass = (jclass)makeGlobal(m_mcClass);

    m_mcGetInstance = getStaticMethodID(m_mcClass,
        Mappings::MC::getInstance, "()Lnet/minecraft/class_310;");
    if (!m_mcGetInstance)
    {
        // Try instance field instead
        jfieldID fid = getStaticFieldID(m_mcClass,
            Mappings::MC::instance, "Lnet/minecraft/class_310;");
        if (fid)
        {
            JNIEnv* e = env();
            jobject local = e ? e->GetStaticObjectField(m_mcClass, fid) : nullptr;
            if (local) m_mcGlobal = makeGlobal(local);
        }
    }
    else
    {
        JNIEnv* e = env();
        jobject local = e ? e->CallStaticObjectMethod(m_mcClass, m_mcGetInstance) : nullptr;
        if (e && e->ExceptionCheck()) { e->ExceptionClear(); local = nullptr; }
        if (local) { m_mcGlobal = makeGlobal(local); e->DeleteLocalRef(local); }
    }

    if (!m_mcGlobal)
        LOG_ERROR("Failed to get MinecraftClient instance");

    return m_mcGlobal;
}

jobject JVM::getPlayer()
{
    jobject mc = getMinecraft();
    if (!mc) return nullptr;

    jfieldID pid = getFieldID(m_mcClass, Mappings::MC::player, "Lnet/minecraft/class_746;");
    if (!pid)
    {
        LOG_ERROR("player field not found");
        return nullptr;
    }
    JNIEnv* e = env();
    return e ? e->GetObjectField(mc, pid) : nullptr;
}

jobject JVM::getWorld()
{
    jobject mc = getMinecraft();
    if (!mc) return nullptr;

    jfieldID wid = getFieldID(m_mcClass, Mappings::MC::world, "Lnet/minecraft/class_638;");
    if (!wid) return nullptr;
    JNIEnv* e = env();
    return e ? e->GetObjectField(mc, wid) : nullptr;
}

jobject JVM::getNetworkHandler()
{
    jobject player = getPlayer();
    if (!player) return nullptr;

    JNIEnv* e = env();
    if (!e) return nullptr;
    jclass playerCls = findClass(Mappings::Cls::ClientPlayerEntity);
    if (!playerCls) return nullptr;

    jfieldID fid = getFieldID(playerCls,
        Mappings::PlayerFields::networkHandler, "Lnet/minecraft/class_634;");
    e->DeleteLocalRef(playerCls);
    if (!fid) return nullptr;
    return e->GetObjectField(player, fid);
}

bool JVM::isScreenOpen()
{
    JNIEnv* e = env();
    jobject mc = getMinecraft();
    if (!e || !mc) return true;
    if (e->PushLocalFrame(8) != 0) return true;

    bool open = true;
    jclass cls = findClass(Mappings::Cls::MinecraftClient);
    jfieldID fid = cls ? getFieldID(cls, Mappings::MC::currentScreen, "Lnet/minecraft/class_437;") : nullptr;
    if (fid)
    {
        jobject screen = e->GetObjectField(mc, fid);
        open = (screen != nullptr);
    }
    e->PopLocalFrame(nullptr);
    return open;
}

// ── Tick hook ────────────────────────────────────────────────

bool JVM::hookTick()
{
    if (!m_jvmti)
    {
        LOG_WARN("No JVMTI; tick hook unavailable");
        return false;
    }

    if (!m_mcClass)
    {
        jclass local = findClass(Mappings::Cls::MinecraftClient);
        if (!local)
        {
            LOG_ERROR("Cannot hook tick: MC class missing");
            return false;
        }
        m_mcClass = (jclass)makeGlobal(local);
        env()->DeleteLocalRef(local);
    }

    m_tickMethod = getMethodID(m_mcClass, Mappings::MC::tick, "()V");
    if (!m_tickMethod)
    {
        LOG_ERROR("tick() method not found");
        return false;
    }

    jvmtiError err = m_jvmti->SetBreakpoint(m_tickMethod, 0);
    if (err != JVMTI_ERROR_NONE)
    {
        LOG_ERROR("SetBreakpoint failed: " + std::to_string(err));
        return false;
    }

    jvmtiEventCallbacks callbacks = {};
    callbacks.Breakpoint = &JVM::breakpointCB;
    m_jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
    m_jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr);

    LOG_INFO("Tick breakpoint set on MinecraftClient.tick()");
    return true;
}

void JVM::unhookTick()
{
    m_tickCallback = nullptr;   // the breakpoint callback becomes a no-op first
    if (!m_jvmti || !m_tickMethod) return;
    m_jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
    m_jvmti->ClearBreakpoint(m_tickMethod, 0);
    jvmtiEventCallbacks none = {};
    m_jvmti->SetEventCallbacks(&none, sizeof(none));
    m_tickMethod = nullptr;
}

void JNICALL JVM::breakpointCB(jvmtiEnv* jvmti, JNIEnv* jni,
    jthread thread, jmethodID method, jlocation location)
{
    auto& inst = instance();
    if (inst.m_tickCallback)
        inst.m_tickCallback();
}
