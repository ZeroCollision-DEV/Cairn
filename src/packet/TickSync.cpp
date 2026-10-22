#include "TickSync.h"
#include "../utils/Logger.h"
#include "../modules/ModuleManager.h"
#include "../core/JVM.h"
#include "PacketManager.h"

TickSync& TickSync::instance()
{
    static TickSync inst;
    return inst;
}

void TickSync::queueAction(std::function<void()>&& action)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.push_back(std::move(action));
}

void TickSync::onTickStart()
{
    JNIEnv* env = JVM::instance().env();
    if (!env) return;

    // Push a local frame to prevent ref leaks across modules
    if (env->PushLocalFrame(256) != 0)
    {
        LOG_WARN("PushLocalFrame failed in onTickStart");
        return;
    }

    m_inTick = true;

    // Re-apply last session's enabled modules once, as soon as a player exists.
    {
        auto& mgr = ModuleManager::instance();
        if (!mgr.enabledRestored())
        {
            jobject player = JVM::instance().getPlayer();
            if (player)
            {
                env->DeleteLocalRef(player);
                mgr.restoreEnabled();
            }
        }
    }

    // 1. Run module ticks — modules may queue packets or call game methods directly
    ModuleManager::instance().tickAll();

    // 2. Flush all queued packets at the start of the tick
    PacketManager::instance().flush();

    // 3. Flush any other pending tick-synced actions
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        local.swap(m_pending);
    }
    for (auto& fn : local)
    {
        try { fn(); }
        catch (...) { LOG_WARN("Exception in tick action"); }
    }

    m_inTick = false;
    env->PopLocalFrame(nullptr);
}
