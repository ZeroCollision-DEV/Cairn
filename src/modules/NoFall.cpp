#include "NoFall.h"
#include "../core/JVM.h"
#include "../core/Mappings.h"
#include "../packet/PacketManager.h"

void NoFall::onTick()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject player = jvm.getPlayer();
    if (!player) return;

    jclass entityCls = jvm.findClass(Mappings::Cls::Entity);
    if (!entityCls) return;

    // Get velocity to check if falling
    jmethodID getVel = jvm.getMethodID(entityCls,
        Mappings::Entity::getVelocity, "()Lnet/minecraft/class_243;");
    if (!getVel) { env->DeleteLocalRef(entityCls); return; }

    jobject vel = env->CallObjectMethod(player, getVel);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(entityCls); return; }
    if (!vel) { env->DeleteLocalRef(entityCls); return; }

    jclass vecCls = jvm.findClass(Mappings::Cls::Vec3d);
    if (!vecCls) { env->DeleteLocalRef(entityCls); env->DeleteLocalRef(vel); return; }

    jfieldID fy = jvm.getFieldID(vecCls, Mappings::Vec3d::y, "D");
    if (!fy)
    {
        env->DeleteLocalRef(entityCls); env->DeleteLocalRef(vel); env->DeleteLocalRef(vecCls);
        return;
    }

    double vy = env->GetDoubleField(vel, fy);

    // If falling (negative Y velocity), queue an onGround packet
    if (vy < -0.3)
    {
        PacketManager::instance().sendOnGround(true);
    }

    env->DeleteLocalRef(entityCls);
    env->DeleteLocalRef(vel);
    env->DeleteLocalRef(vecCls);
}
