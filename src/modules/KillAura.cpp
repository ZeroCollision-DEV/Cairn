#include "KillAura.h"
#include "../core/JVM.h"
#include "../core/Mappings.h"
#include "../packet/PacketManager.h"
#include "../utils/Logger.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void KillAura::onEnable()
{
    m_delayCounter = 0;
}

void KillAura::onTick()
{
    if (m_delayCounter > 0)
    {
        m_delayCounter--;
        return;
    }

    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject player = jvm.getPlayer();
    if (!player) return;

    jobject world = jvm.getWorld();
    if (!world) return;

    // Use a local frame for clean ref management
    if (env->PushLocalFrame(32) != 0)
    {
        LOG_WARN("KillAura: PushLocalFrame failed");
        return;
    }

    // ── Get player position ──────────────────────────────────
    jclass entityCls = jvm.findClass(Mappings::Cls::Entity);
    if (!entityCls) { env->PopLocalFrame(nullptr); return; }

    jmethodID getX = jvm.getMethodID(entityCls, Mappings::Entity::getX, "()D");
    jmethodID getY = jvm.getMethodID(entityCls, Mappings::Entity::getY, "()D");
    jmethodID getZ = jvm.getMethodID(entityCls, Mappings::Entity::getZ, "()D");
    jmethodID getId = jvm.getMethodID(entityCls, Mappings::Entity::getId, "()I");
    jmethodID isAlive = jvm.getMethodID(entityCls, Mappings::Entity::isAlive, "()Z");

    if (!getX || !getY || !getZ || !getId || !isAlive)
    {
        env->PopLocalFrame(nullptr);
        return;
    }

    double px = env->CallDoubleMethod(player, getX);
    double py = env->CallDoubleMethod(player, getY) + 1.62; // eye height
    double pz = env->CallDoubleMethod(player, getZ);
    int playerId = env->CallIntMethod(player, getId);

    // ── Get entity iterable from world ───────────────────────
    jclass worldCls = jvm.findClass(Mappings::Cls::ClientWorld);
    if (!worldCls) { env->PopLocalFrame(nullptr); return; }

    jobject iterable = nullptr;
    jmethodID getEntities = jvm.getMethodID(worldCls,
        Mappings::World::getEntities, "()Ljava/lang/Iterable;");
    if (getEntities)
    {
        iterable = env->CallObjectMethod(world, getEntities);
        if (env->ExceptionCheck()) { env->ExceptionClear(); iterable = nullptr; }
    }

    if (!iterable)
    {
        LOG_WARN("KillAura: could not obtain entity list from world");
        env->PopLocalFrame(nullptr);
        return;
    }

    // ── Iterate entities ─────────────────────────────────────
    jclass iterCls = env->FindClass("java/util/Iterator");
    jclass itblCls = env->FindClass("java/lang/Iterable");
    jmethodID iterMid = env->GetMethodID(itblCls, "iterator", "()Ljava/util/Iterator;");
    jmethodID hasNext = env->GetMethodID(iterCls, "hasNext", "()Z");
    jmethodID nextMid = env->GetMethodID(iterCls, "next", "()Ljava/lang/Object;");

    jobject iterator = env->CallObjectMethod(iterable, iterMid);
    if (!iterator)
    {
        env->PopLocalFrame(nullptr);
        return;
    }

    jclass livingCls = jvm.findClass(Mappings::Cls::LivingEntity);

    jobject bestTarget = nullptr;
    double bestDist = 1e9;

    while (env->CallBooleanMethod(iterator, hasNext))
    {
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }

        jobject entity = env->CallObjectMethod(iterator, nextMid);
        if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
        if (!entity) continue;

        // Skip self
        if (env->IsSameObject(entity, player))
        {
            env->DeleteLocalRef(entity);
            continue;
        }

        // Skip dead
        if (!env->CallBooleanMethod(entity, isAlive))
        {
            env->DeleteLocalRef(entity);
            continue;
        }

        // Skip non-living (if we can check)
        if (livingCls && !env->IsInstanceOf(entity, livingCls))
        {
            env->DeleteLocalRef(entity);
            continue;
        }

        // Distance check
        double ex = env->CallDoubleMethod(entity, getX);
        double ey = env->CallDoubleMethod(entity, getY);
        double ez = env->CallDoubleMethod(entity, getZ);

        double dx = ex - px;
        double dy = ey - py;
        double dz = ez - pz;
        double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        float range = getFloat("Range", 4.5f);
        if (dist <= range && dist < bestDist)
        {
            bestDist = dist;
            if (bestTarget) env->DeleteLocalRef(bestTarget);
            bestTarget = env->NewLocalRef(entity);
        }

        env->DeleteLocalRef(entity);
    }

    // ── Attack best target ───────────────────────────────────
    if (bestTarget)
    {
        // Get target position for rotation
        double ex = env->CallDoubleMethod(bestTarget, getX);
        double ey = env->CallDoubleMethod(bestTarget, getY);
        double ez = env->CallDoubleMethod(bestTarget, getZ);

        double dx = ex - px;
        double dy = (ey + 1.0) - py; // approximate target center
        double dz = ez - pz;

        double distXZ = std::sqrt(dx * dx + dz * dz);
        float yaw = (float)(std::atan2(-dx, dz) * 180.0 / M_PI);
        float pitch = (float)(-std::atan2(dy, distXZ) * 180.0 / M_PI);

        // Clamp pitch
        if (pitch > 90.0f) pitch = 90.0f;
        if (pitch < -90.0f) pitch = -90.0f;

        // Set player rotation via fields
        jfieldID yawF = jvm.getFieldID(entityCls, Mappings::Entity::yawField, "F");
        jfieldID pitchF = jvm.getFieldID(entityCls, Mappings::Entity::pitchField, "F");
        jfieldID prevYawF = jvm.getFieldID(entityCls, Mappings::Entity::prevYaw, "F");
        jfieldID prevPitchF = jvm.getFieldID(entityCls, Mappings::Entity::prevPitch, "F");

        if (yawF)   env->SetFloatField(player, yawF, yaw);
        if (pitchF) env->SetFloatField(player, pitchF, pitch);
        if (prevYawF)   env->SetFloatField(player, prevYawF, yaw);
        if (prevPitchF) env->SetFloatField(player, prevPitchF, pitch);

        // Attack through the interaction manager, which sends the attack packet to the server
        // and then runs the local attack (calling PlayerEntity.attack alone would not tell the server).
        jobject mc = jvm.getMinecraft();
        jclass mcCls = jvm.findClass(Mappings::Cls::MinecraftClient);
        jclass imCls = jvm.findClass(Mappings::Cls::ClientPlayerInteractionManager);
        if (mc && mcCls && imCls)
        {
            jfieldID imField = jvm.getFieldID(mcCls, Mappings::MC::interactionManager, "Lnet/minecraft/class_636;");
            jmethodID attackMid = jvm.getMethodID(imCls, Mappings::Interaction::attackEntity,
                "(Lnet/minecraft/class_1657;Lnet/minecraft/class_1297;)V");
            if (imField && attackMid)
            {
                jobject im = env->GetObjectField(mc, imField);
                if (im)
                {
                    env->CallVoidMethod(im, attackMid, player, bestTarget);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }
            }
        }

        // Reset delay
        m_delayCounter = getInt("Delay", 2);

        env->DeleteLocalRef(bestTarget);
    }

    env->PopLocalFrame(nullptr);
}
