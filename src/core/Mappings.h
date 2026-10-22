#pragma once
#include <jni.h>
#include <jvmti.h>

#ifndef JNI_VERSION_21
#define JNI_VERSION_21 0x00150000
#endif

// ════════════════════════════════════════════════════════════════════════════
//  Minecraft 1.21.11 -- Fabric intermediary names
//
//  Every entry below was checked against the FabricMC/yarn mapping files for
//  Minecraft 1.21.11 (branch 1.21.11, commit ee98590897c919286b96b7c7f2c89ae4ac737762):
//  the intermediary name exists, is declared on the class given, and the JNI
//  descriptor matches (descriptors in yarn use intermediary class names, as JNI does
//  under Fabric Loader at runtime).
//
//  "Verified" here means: confirmed against mapping DATA. It does NOT mean the
//  code that uses the name has been run in the game.
//
//  Inheritance is not stored in yarn's files, so a member declared on a superclass or
//  interface (marked "inherited") is looked up through a subclass by JNI and relies on
//  that class relationship, which is not itself checked here.
//
//  To add an entry: find it in the yarn .mapping files, copy the intermediary name and
//  descriptor exactly, and record which class declares it. Never guess or reuse a name.
// ════════════════════════════════════════════════════════════════════════════

namespace Mappings
{
    // ── Classes ──────────────────────────────────────────────
    namespace Cls
    {
        constexpr const char* MinecraftClient            = "net/minecraft/class_310";   // client.MinecraftClient
        constexpr const char* ClientPlayerEntity         = "net/minecraft/class_746";   // client.network.ClientPlayerEntity
        constexpr const char* ClientWorld                = "net/minecraft/class_638";   // client.world.ClientWorld
        constexpr const char* ClientPlayNetworkHandler   = "net/minecraft/class_634";   // client.network.ClientPlayNetworkHandler
        constexpr const char* ClientPlayerInteractionManager = "net/minecraft/class_636"; // client.network.ClientPlayerInteractionManager
        constexpr const char* Entity                     = "net/minecraft/class_1297";  // entity.Entity
        constexpr const char* LivingEntity               = "net/minecraft/class_1309";  // entity.LivingEntity
        constexpr const char* PlayerEntity               = "net/minecraft/class_1657";  // entity.player.PlayerEntity
        constexpr const char* Vec3d                      = "net/minecraft/class_243";   // util.math.Vec3d

        namespace Packet   // network.packet.c2s.play.*
        {
            // PlayerMoveC2SPacket is class_2828; its nested packets are named with '$'.
            constexpr const char* PlayerMovePositionAndOnGround = "net/minecraft/class_2828$class_2829"; // (DDDZZ)V
            constexpr const char* PlayerMoveC2SPacketFull       = "net/minecraft/class_2828$class_2830"; // Full (DDDFFZZ)V
            constexpr const char* PlayerMoveC2SPacketLook       = "net/minecraft/class_2828$class_2831"; // LookAndOnGround (FFZZ)V
            constexpr const char* PlayerMoveC2SPacketOnGround   = "net/minecraft/class_2828$class_5911"; // OnGroundOnly (ZZ)V
            constexpr const char* UpdateSelectedSlotC2SPacket   = "net/minecraft/class_2868";            // (I)V
            // The trailing boolean of the move packets is `horizontalCollision`.
        }
    }

    // ── MinecraftClient (class_310) ──────────────────────────
    namespace MC
    {
        constexpr const char* tick               = "method_1574";   // ()V
        constexpr const char* getInstance        = "method_1551";   // static ()Lnet/minecraft/class_310;
        constexpr const char* instance           = "field_1700";    // static Lnet/minecraft/class_310;
        constexpr const char* player             = "field_1724";    // Lnet/minecraft/class_746;
        constexpr const char* world              = "field_1687";    // Lnet/minecraft/class_638;
        constexpr const char* options            = "field_1690";    // Lnet/minecraft/class_315;
        constexpr const char* interactionManager = "field_1761";    // Lnet/minecraft/class_636;
        constexpr const char* currentScreen      = "field_1755";    // Lnet/minecraft/class_437;
    }

    // ── ClientPlayerEntity (class_746) ───────────────────────
    namespace PlayerFields
    {
        constexpr const char* networkHandler = "field_3944";        // Lnet/minecraft/class_634;
    }

    // ── PlayerEntity (class_1657) ────────────────────────────
    namespace Player
    {
        constexpr const char* getAbilities = "method_31549";        // ()Lnet/minecraft/class_1656;
        // Entity.setSprinting, looked up through ClientPlayerEntity (inherited)
        constexpr const char* setSprinting = "method_5728";         // (Z)V
    }

    // ── ClientPlayerInteractionManager (class_636) ───────────
    namespace Interaction
    {
        constexpr const char* attackEntity = "method_2918";         // (Lnet/minecraft/class_1657;Lnet/minecraft/class_1297;)V
    }

    // ── ClientPlayNetworkHandler (class_634) ─────────────────
    namespace NetHandler
    {
        // Declared on ClientCommonNetworkHandler (class_8673); inherited by class_634.
        constexpr const char* sendPacket = "method_52787";          // (Lnet/minecraft/class_2596;)V
    }

    // ── Entity (class_1297) ──────────────────────────────────
    namespace Entity
    {
        constexpr const char* getId        = "method_5628";    // ()I   inherited (declared on EntityLike, class_5568)
        constexpr const char* getX         = "method_23317";   // ()D
        constexpr const char* getY         = "method_23318";   // ()D
        constexpr const char* getZ         = "method_23321";   // ()D
        constexpr const char* isAlive      = "method_5805";    // ()Z
        constexpr const char* getVelocity  = "method_18798";   // ()Lnet/minecraft/class_243;
        constexpr const char* setVelocityV = "method_18799";   // (Lnet/minecraft/class_243;)V
        constexpr const char* setVelocityD = "method_18800";   // (DDD)V
        constexpr const char* yawField     = "field_6031";     // F
        constexpr const char* pitchField   = "field_5965";     // F
        constexpr const char* prevYaw      = "field_5982";     // F  (lastYaw)
        constexpr const char* prevPitch    = "field_6004";     // F  (lastPitch)
    }

    // ── Vec3d (class_243): public final double fields ────────
    namespace Vec3d
    {
        constexpr const char* x = "field_1352";   // D
        constexpr const char* y = "field_1351";   // D
        constexpr const char* z = "field_1350";   // D
        // constructor: <init> (DDD)V
    }

    // ── PlayerAbilities (class_1656) ─────────────────────────
    namespace Abilities
    {
        constexpr const char* cls         = "net/minecraft/class_1656";
        constexpr const char* flying      = "field_7479";   // Z
        constexpr const char* allowFlying = "field_7478";   // Z
    }

    // ── GameOptions (class_315) / SimpleOption (class_7172) ──
    namespace Options
    {
        constexpr const char* cls         = "net/minecraft/class_315";
        constexpr const char* gammaOption = "field_1840";   // Lnet/minecraft/class_7172;
    }

    namespace SimpleOption
    {
        constexpr const char* cls      = "net/minecraft/class_7172";
        constexpr const char* getValue = "method_41753";    // ()Ljava/lang/Object;
        constexpr const char* setValue = "method_41748";    // (Ljava/lang/Object;)V
    }

    // ── ClientWorld (class_638) ──────────────────────────────
    namespace World
    {
        constexpr const char* getEntities = "method_18112"; // ()Ljava/lang/Iterable;
    }
}
