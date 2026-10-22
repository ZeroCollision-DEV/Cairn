#include "PacketManager.h"
#include "../core/JVM.h"
#include "../core/Mappings.h"
#include "../utils/Logger.h"
#include "TickSync.h"

#include <cstdarg>

PacketManager& PacketManager::instance()
{
    static PacketManager inst;
    return inst;
}

jobject PacketManager::constructPacket(const char* className, const char* ctorSig, ...)
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return nullptr;

    jclass cls = jvm.findClass(className);
    if (!cls)
    {
        LOG_ERROR(std::string("Packet class not found: ") + className);
        return nullptr;
    }

    // Parse the signature to get arg count
    // Simple approach: count the non-paren chars in ctorSig
    // We use va_list to extract args
    jmethodID ctor = env->GetMethodID(cls, "<init>", ctorSig);
    if (!ctor)
    {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        LOG_ERROR("Packet ctor not found");
        return nullptr;
    }

    // Parse signature and build jvalue array
    // Supported types: D (double), F (float), Z (boolean), I (int)
    std::string sig(ctorSig);
    size_t start = sig.find('(');
    size_t end   = sig.find(')');
    if (start == std::string::npos || end == std::string::npos)
    {
        env->DeleteLocalRef(cls);
        return nullptr;
    }

    std::string args = sig.substr(start + 1, end - start - 1);
    std::vector<jvalue> jvals;
    va_list vl;
    va_start(vl, ctorSig);

    for (char c : args)
    {
        jvalue v;
        switch (c)
        {
        case 'D': v.d = va_arg(vl, double);    break;
        case 'F': v.f = (float)va_arg(vl, double); break;
        case 'Z': v.z = (jboolean)va_arg(vl, int); break;
        case 'I': v.i = va_arg(vl, int);        break;
        case 'J': v.j = va_arg(vl, jlong);      break;
        default:
            // Skip complex types for now
            va_end(vl);
            env->DeleteLocalRef(cls);
            return nullptr;
        }
        jvals.push_back(v);
    }
    va_end(vl);

    jobject pkt = env->NewObjectA(cls, ctor, jvals.data());
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        LOG_ERROR("Failed to construct packet");
        return nullptr;
    }

    return pkt;
}

void PacketManager::sendPacket(jobject packet)
{
    if (!packet) return;
    auto& jvm = JVM::instance();
    jobject global = jvm.makeGlobal(packet);
    jvm.env()->DeleteLocalRef(packet);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_queued.push_back(global);
}

void PacketManager::sendPositionFull(double x, double y, double z,
    float yaw, float pitch, bool onGround)
{
    jobject pkt = constructPacket(
        Mappings::Cls::Packet::PlayerMoveC2SPacketFull,
        "(DDDFFZZ)V",
        x, y, z, yaw, pitch, onGround ? 1 : 0, 0);   // last arg: horizontalCollision
    sendPacket(pkt);
}

void PacketManager::sendLook(float yaw, float pitch, bool onGround)
{
    jobject pkt = constructPacket(
        Mappings::Cls::Packet::PlayerMoveC2SPacketLook,
        "(FFZZ)V",
        yaw, pitch, onGround ? 1 : 0, 0);
    sendPacket(pkt);
}

void PacketManager::sendOnGround(bool onGround)
{
    jobject pkt = constructPacket(
        Mappings::Cls::Packet::PlayerMoveC2SPacketOnGround,
        "(ZZ)V",
        onGround ? 1 : 0, 0);
    sendPacket(pkt);
}

void PacketManager::sendPlayerAction(int action, int x, int y, int z, int face)
{
    // PlayerActionC2SPacket(PlayerActionC2SPacket$Action, BlockPos, Direction)
    // For simplicity, we pass the raw int values; the actual constructor
    // takes enum objects, so this needs JNI enum resolution.
    // This is a placeholder for the pattern — modules construct their own.
    LOG_WARN("sendPlayerAction: enum-based packets need JNI enum resolution");
}

void PacketManager::sendEntityUse(int entityId, int actionType)
{
    // PlayerInteractEntityC2SPacket requires an enum for action type.
    // Similar to above — modules handle enum construction.
    LOG_WARN("sendEntityUse: enum-based packets need JNI enum resolution");
}

void PacketManager::sendSelectedSlot(int slot)
{
    jobject pkt = constructPacket(
        Mappings::Cls::Packet::UpdateSelectedSlotC2SPacket,
        "(I)V",
        slot);
    sendPacket(pkt);
}

void PacketManager::sendHandSwing(int hand)
{
    // HandSwingC2SPacket(Hand) — Hand is an enum
    LOG_WARN("sendHandSwing: enum-based packets need JNI enum resolution");
}

void PacketManager::flush()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject handler = jvm.getNetworkHandler();
    if (!handler)
    {
        LOG_WARN("NetworkHandler null during flush");
        clearAll();
        return;
    }

    jclass nhCls = env->FindClass(Mappings::Cls::ClientPlayNetworkHandler);
    if (!nhCls)
    {
        env->ExceptionClear();
        clearAll();
        return;
    }

    jmethodID sendMid = env->GetMethodID(nhCls,
        Mappings::NetHandler::sendPacket,
        "(Lnet/minecraft/class_2596;)V");
    env->DeleteLocalRef(nhCls);
    if (!sendMid)
    {
        env->ExceptionClear();
        LOG_ERROR("sendPacket method not found on NetworkHandler");
        clearAll();
        return;
    }

    std::vector<jobject> local;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        local.swap(m_queued);
    }

    for (auto& pkt : local)
    {
        env->CallVoidMethod(handler, sendMid, pkt);
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            LOG_ERROR("sendPacket threw");
        }
        jvm.releaseGlobal(pkt);
    }
}

void PacketManager::clearAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& jvm = JVM::instance();
    for (auto& pkt : m_queued)
        jvm.releaseGlobal(pkt);
    m_queued.clear();
}
