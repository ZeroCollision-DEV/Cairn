#pragma once
#include <jni.h>
#include <vector>
#include <mutex>
#include <string>

class PacketManager
{
public:
    static PacketManager& instance();

    // Queue a packet to send at the start of the next tick.
    // The jobject is wrapped in a global ref; released after flush.
    void sendPacket(jobject packet);

    // Convenience: queue a full-position packet
    void sendPositionFull(double x, double y, double z,
        float yaw, float pitch, bool onGround);

    // Convenience: queue a look-only packet
    void sendLook(float yaw, float pitch, bool onGround);

    // Convenience: queue an on-ground-only packet
    void sendOnGround(bool onGround);

    // Convenience: queue a player action (start/stop/abort breaking)
    void sendPlayerAction(int action, int x, int y, int z, int face);

    // Convenience: queue an entity interact (attack)
    void sendEntityUse(int entityId, int actionType);

    // Convenience: queue a hotbar slot change
    void sendSelectedSlot(int slot);

    // Convenience: queue a hand swing
    void sendHandSwing(int hand);

    // Flush all queued packets — called by TickSync at tick start
    void flush();

    // Release all global refs (shutdown)
    void clearAll();

private:
    PacketManager() = default;

    std::vector<jobject> m_queued;
    std::mutex m_mutex;

    // Construct a packet jobject from class name + constructor sig + args
    jobject constructPacket(const char* className, const char* ctorSig, ...);
};
