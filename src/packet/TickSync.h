#pragma once
#include <vector>
#include <functional>
#include <mutex>

class TickSync
{
public:
    static TickSync& instance();

    // Queue an action for the start of the next tick
    void queueAction(std::function<void()>&& action);

    // Called at the start of each game tick (from JVMTI breakpoint or fallback)
    void onTickStart();

    bool isInTick() const { return m_inTick; }

private:
    TickSync() = default;

    std::vector<std::function<void()>> m_pending;
    std::mutex m_mutex;
    bool m_inTick = false;
};
