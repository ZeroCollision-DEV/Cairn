#include "FullBright.h"
#include "../core/JVM.h"
#include "../core/Mappings.h"
#include "../utils/Logger.h"

void FullBright::onEnable()
{
    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject mc = jvm.getMinecraft();
    if (!mc) return;

    jclass mcCls = jvm.findClass(Mappings::Cls::MinecraftClient);
    if (!mcCls) return;

    // Get options field
    jfieldID optField = jvm.getFieldID(mcCls,
        Mappings::MC::options, "Lnet/minecraft/class_315;");
    if (!optField) { env->DeleteLocalRef(mcCls); return; }

    jobject options = env->GetObjectField(mc, optField);
    if (!options) { env->DeleteLocalRef(mcCls); return; }

    // Get gamma option field
    jclass optCls = jvm.findClass(Mappings::Options::cls);
    if (!optCls) { env->DeleteLocalRef(mcCls); env->DeleteLocalRef(options); return; }

    jfieldID gammaField = jvm.getFieldID(optCls,
        Mappings::Options::gammaOption, "Lnet/minecraft/class_7172;");
    if (!gammaField)
    {
        env->DeleteLocalRef(mcCls); env->DeleteLocalRef(options); env->DeleteLocalRef(optCls);
        return;
    }

    jobject gammaOpt = env->GetObjectField(options, gammaField);
    if (!gammaOpt)
    {
        env->DeleteLocalRef(mcCls); env->DeleteLocalRef(options); env->DeleteLocalRef(optCls);
        return;
    }

    // Save original value
    jclass soCls = jvm.findClass(Mappings::SimpleOption::cls);
    if (soCls)
    {
        jmethodID getValue = jvm.getMethodID(soCls,
            Mappings::SimpleOption::getValue, "()Ljava/lang/Object;");
        if (getValue)
        {
            jobject val = env->CallObjectMethod(gammaOpt, getValue);
            if (val && env->IsInstanceOf(val, env->FindClass("java/lang/Double")))
            {
                jclass doubleCls = env->FindClass("java/lang/Double");
                jmethodID doubleValue = env->GetMethodID(doubleCls, "doubleValue", "()D");
                m_originalGamma = env->CallDoubleMethod(val, doubleValue);
                m_hasOriginal = true;
                env->DeleteLocalRef(doubleCls);
            }
            if (val) env->DeleteLocalRef(val);
        }
    }

    // Set gamma to 15.0
    if (soCls)
    {
        jmethodID setValue = jvm.getMethodID(soCls,
            Mappings::SimpleOption::setValue, "(Ljava/lang/Object;)V");
        if (setValue)
        {
            jclass doubleCls = env->FindClass("java/lang/Double");
            jmethodID valueOf = env->GetStaticMethodID(doubleCls, "valueOf", "(D)Ljava/lang/Double;");
            jobject newGamma = env->CallStaticObjectMethod(doubleCls, valueOf, 15.0);
            env->CallVoidMethod(gammaOpt, setValue, newGamma);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(newGamma);
            env->DeleteLocalRef(doubleCls);
        }
    }

    env->DeleteLocalRef(mcCls);
    env->DeleteLocalRef(options);
    env->DeleteLocalRef(optCls);
    if (soCls) env->DeleteLocalRef(soCls);
    env->DeleteLocalRef(gammaOpt);
}

void FullBright::onDisable()
{
    if (!m_hasOriginal) return;

    auto& jvm = JVM::instance();
    JNIEnv* env = jvm.env();
    if (!env) return;

    jobject mc = jvm.getMinecraft();
    if (!mc) return;

    jclass mcCls = jvm.findClass(Mappings::Cls::MinecraftClient);
    if (!mcCls) return;

    jfieldID optField = jvm.getFieldID(mcCls,
        Mappings::MC::options, "Lnet/minecraft/class_315;");
    if (!optField) { env->DeleteLocalRef(mcCls); return; }

    jobject options = env->GetObjectField(mc, optField);
    if (!options) { env->DeleteLocalRef(mcCls); return; }

    jclass optCls = jvm.findClass(Mappings::Options::cls);
    if (!optCls) { env->DeleteLocalRef(mcCls); env->DeleteLocalRef(options); return; }

    jfieldID gammaField = jvm.getFieldID(optCls,
        Mappings::Options::gammaOption, "Lnet/minecraft/class_7172;");
    if (!gammaField)
    {
        env->DeleteLocalRef(mcCls); env->DeleteLocalRef(options); env->DeleteLocalRef(optCls);
        return;
    }

    jobject gammaOpt = env->GetObjectField(options, gammaField);
    if (!gammaOpt)
    {
        env->DeleteLocalRef(mcCls); env->DeleteLocalRef(options); env->DeleteLocalRef(optCls);
        return;
    }

    jclass soCls = jvm.findClass(Mappings::SimpleOption::cls);
    if (soCls)
    {
        jmethodID setValue = jvm.getMethodID(soCls,
            Mappings::SimpleOption::setValue, "(Ljava/lang/Object;)V");
        if (setValue)
        {
            jclass doubleCls = env->FindClass("java/lang/Double");
            jmethodID valueOf = env->GetStaticMethodID(doubleCls, "valueOf", "(D)Ljava/lang/Double;");
            jobject orig = env->CallStaticObjectMethod(doubleCls, valueOf, m_originalGamma);
            env->CallVoidMethod(gammaOpt, setValue, orig);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(orig);
            env->DeleteLocalRef(doubleCls);
        }
        env->DeleteLocalRef(soCls);
    }

    env->DeleteLocalRef(mcCls);
    env->DeleteLocalRef(options);
    env->DeleteLocalRef(optCls);
    env->DeleteLocalRef(gammaOpt);
    m_hasOriginal = false;
}
