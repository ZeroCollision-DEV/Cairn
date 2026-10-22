#include "Sprint.h"
#include "../core/JVM.h"
#include "../core/Mappings.h"

void Sprint::onTick()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject player = jvm.getPlayer();
    if (!player) return;

    jclass playerCls = jvm.findClass(Mappings::Cls::ClientPlayerEntity);
    if (!playerCls) return;

    jmethodID setSprint = jvm.getMethodID(playerCls,
        Mappings::Player::setSprinting, "(Z)V");
    if (setSprint)
        env->CallVoidMethod(player, setSprint, JNI_TRUE);

    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(playerCls);
}
