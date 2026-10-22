#include "Speed.h"
#include "../core/JVM.h"
#include "../core/Mappings.h"
#include "../utils/Logger.h"
#include <cmath>

void Speed::onTick()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject player = jvm.getPlayer();
    if (!player) return;

    jclass entityCls = jvm.findClass(Mappings::Cls::Entity);
    if (!entityCls) return;

    // Get current velocity
    jmethodID getVel = jvm.getMethodID(entityCls,
        Mappings::Entity::getVelocity, "()Lnet/minecraft/class_243;");
    if (!getVel) { env->DeleteLocalRef(entityCls); return; }

    jobject vel = env->CallObjectMethod(player, getVel);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(entityCls); return; }
    if (!vel) { env->DeleteLocalRef(entityCls); return; }

    // Extract X, Y, Z
    jclass vecCls = jvm.findClass(Mappings::Cls::Vec3d);
    if (!vecCls) { env->DeleteLocalRef(entityCls); env->DeleteLocalRef(vel); return; }

    jfieldID fx = jvm.getFieldID(vecCls, Mappings::Vec3d::x, "D");
    jfieldID fy = jvm.getFieldID(vecCls, Mappings::Vec3d::y, "D");
    jfieldID fz = jvm.getFieldID(vecCls, Mappings::Vec3d::z, "D");
    if (!fx || !fy || !fz)
    {
        env->DeleteLocalRef(entityCls); env->DeleteLocalRef(vel); env->DeleteLocalRef(vecCls);
        return;
    }

    double vx = env->GetDoubleField(vel, fx);
    double vy = env->GetDoubleField(vel, fy);
    double vz = env->GetDoubleField(vel, fz);

    // Only boost if moving
    float factor = getFloat("Multiplier", 1.5f);
    double horizSpeed = std::sqrt(vx * vx + vz * vz);

    if (horizSpeed > 0.01)
    {
        vx *= factor;
        vz *= factor;

        // Try setVelocity(DDD) first
        jmethodID setVelD = jvm.getMethodID(entityCls,
            Mappings::Entity::setVelocityD, "(DDD)V");
        if (setVelD)
        {
            env->CallVoidMethod(player, setVelD, vx, vy, vz);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        else
        {
            // Fallback: setVelocity(Vec3d)
            jmethodID ctor = env->GetMethodID(vecCls, "<init>", "(DDD)V");
            jmethodID setVelV = jvm.getMethodID(entityCls,
                Mappings::Entity::setVelocityV, "(Lnet/minecraft/class_243;)V");
            if (ctor && setVelV)
            {
                jobject newVel = env->NewObject(vecCls, ctor, vx, vy, vz);
                env->CallVoidMethod(player, setVelV, newVel);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(newVel);
            }
        }
    }

    env->DeleteLocalRef(entityCls);
    env->DeleteLocalRef(vel);
    env->DeleteLocalRef(vecCls);
}
