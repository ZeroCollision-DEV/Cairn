#include "Fly.h"
#include "../core/JVM.h"
#include "../core/Mappings.h"
#include "../packet/PacketManager.h"
#include "../utils/Logger.h"
#include <Windows.h>

static void setAbility(JNIEnv* env, jobject abilities, const char* fieldName, jboolean val)
{
    auto& jvm = JVM::instance();
    jclass abCls = jvm.findClass(Mappings::Abilities::cls);
    if (!abCls) return;

    jfieldID fid = jvm.getFieldID(abCls, fieldName, "Z");
    if (fid)
    {
        env->SetBooleanField(abilities, fid, val);
    }
    env->DeleteLocalRef(abCls);
}

void Fly::onEnable()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject player = jvm.getPlayer();
    if (!player) return;

    jclass playerCls = jvm.findClass(Mappings::Cls::PlayerEntity);
    if (!playerCls) return;

    jmethodID getAbilities = jvm.getMethodID(playerCls,
        Mappings::Player::getAbilities, "()Lnet/minecraft/class_1656;");
    if (!getAbilities) { env->DeleteLocalRef(playerCls); return; }

    jobject abilities = env->CallObjectMethod(player, getAbilities);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(playerCls); return; }
    if (!abilities) { env->DeleteLocalRef(playerCls); return; }

    setAbility(env, abilities, Mappings::Abilities::allowFlying, JNI_TRUE);
    setAbility(env, abilities, Mappings::Abilities::flying, JNI_TRUE);

    env->DeleteLocalRef(playerCls);
    env->DeleteLocalRef(abilities);
    LOG_INFO("Fly enabled");
}

void Fly::onDisable()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject player = jvm.getPlayer();
    if (!player) return;

    jclass playerCls = jvm.findClass(Mappings::Cls::PlayerEntity);
    if (!playerCls) return;

    jmethodID getAbilities = jvm.getMethodID(playerCls,
        Mappings::Player::getAbilities, "()Lnet/minecraft/class_1656;");
    if (!getAbilities) { env->DeleteLocalRef(playerCls); return; }

    jobject abilities = env->CallObjectMethod(player, getAbilities);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(playerCls); return; }
    if (!abilities) { env->DeleteLocalRef(playerCls); return; }

    setAbility(env, abilities, Mappings::Abilities::flying, JNI_FALSE);

    env->DeleteLocalRef(playerCls);
    env->DeleteLocalRef(abilities);
    LOG_INFO("Fly disabled");
}

void Fly::onTick()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject player = jvm.getPlayer();
    if (!player) return;

    // Re-assert flying every tick (game may reset it)
    jclass playerCls = jvm.findClass(Mappings::Cls::PlayerEntity);
    if (!playerCls) return;

    jmethodID getAbilities = jvm.getMethodID(playerCls,
        Mappings::Player::getAbilities, "()Lnet/minecraft/class_1656;");
    if (!getAbilities) { env->DeleteLocalRef(playerCls); return; }

    jobject abilities = env->CallObjectMethod(player, getAbilities);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(playerCls); return; }
    if (!abilities) { env->DeleteLocalRef(playerCls); return; }

    setAbility(env, abilities, Mappings::Abilities::flying, JNI_TRUE);
    setAbility(env, abilities, Mappings::Abilities::allowFlying, JNI_TRUE);

    // Anti-kick: every 20 ticks, send a slight downward position offset
    m_tickCounter++;
    if (getBool("AntiKick", true) && m_tickCounter >= 20)
    {
        m_tickCounter = 0;

        // Get player position for the packet
        jclass eCls = jvm.findClass(Mappings::Cls::Entity);
        if (eCls)
        {
            jmethodID getX = jvm.getMethodID(eCls, Mappings::Entity::getX, "()D");
            jmethodID getY = jvm.getMethodID(eCls, Mappings::Entity::getY, "()D");
            jmethodID getZ = jvm.getMethodID(eCls, Mappings::Entity::getZ, "()D");

            if (getX && getY && getZ)
            {
                double px = env->CallDoubleMethod(player, getX);
                double py = env->CallDoubleMethod(player, getY);
                double pz = env->CallDoubleMethod(player, getZ);

                // Queue a position packet with slight downward offset
                PacketManager::instance().sendPositionFull(
                    px, py - 0.1, pz, 0.0f, 0.0f, true);
            }
            env->DeleteLocalRef(eCls);
        }
    }

    env->DeleteLocalRef(playerCls);
    env->DeleteLocalRef(abilities);
}
