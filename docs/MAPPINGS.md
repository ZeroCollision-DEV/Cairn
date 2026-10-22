# Minecraft mapping verification (1.21.11)

**Source of truth:** FabricMC/yarn, branch `1.21.11`, commit `ee98590897c919286b96b7c7f2c89ae4ac737762`
(mapping files read from a shallow clone; yarn records intermediary name, readable name and descriptor).
**Tool:** `tools/verify_mappings.py` - last run: `RESULT: ALL VERIFIED` (40 member/constructor entries, all classes present,
no unlisted `field_/method_/class_` literal under the staged sources).

**What this proves:** each name exists, is declared on the stated class, and has the stated descriptor in the 1.21.11
mapping data. **What it does not prove:** that the game behaves as the modules assume, that inherited lookups resolve
(yarn does not record inheritance), or that anything works at runtime. No module has been run in the game.

## The original `Mappings.h` was largely wrong

Compared with the yarn data, the file shipped with the project had these confirmed errors (all fixed):

| Item | Old value | Actual (yarn 1.21.11) |
|---|---|---|
| `Entity.getZ` | `method_23319` (that is `getRandomBodyY`) | `method_23321` |
| `Entity.getVelocity` | `method_5669` (`getAir`) | `method_18798` |
| `Entity.setVelocity(DDD)` | `method_5668` (`getRootVehicle`) | `method_18800` |
| `Entity.setVelocity(Vec3d)` | `method_5670` (`baseTick`) | `method_18799` |
| `Entity.isAlive` | `method_5685` (`getPassengerList`) | `method_5805` |
| `Entity.setSprinting` | `method_5667` (`getUuid` on another interface) | `method_5728` |
| `Entity` yaw / pitch / lastYaw / lastPitch | `field_5985` (does not exist) / `field_5986` (`id`) / `field_5990` (`FLAGS`) / `field_5991` (does not exist) | `field_6031` / `field_5965` / `field_5982` / `field_6004` |
| `PlayerEntity.getAbilities` | `method_31543` (a `FuzzyPositions` method) | `method_31549` |
| `PlayerAbilities.flying` / `allowFlying` | `field_7465` / `field_7467` (neither exists) | `field_7479` / `field_7478` |
| `GameOptions.gamma` | `field_2162` (does not exist) | `field_1840` |
| `SimpleOption.getValue` / `setValue` | `method_41798` (`GameOptions.getChunkBuilderMode`) / `method_41799` (unrelated helper) | `method_41753` / `method_41748` |
| `MinecraftClient.instance` (static) | `field_1688` (does not exist) | `field_1700` |
| `ClientPlayNetworkHandler` class | `class_2596` (that is `Packet`) | `class_634` |
| `ClientPlayerEntity.networkHandler` | `field_26696` (does not exist), type `class_2596` | `field_3944`, type `class_634` |
| `sendPacket` | `method_2883` (does not exist) | `method_52787`, declared on `ClientCommonNetworkHandler` (`class_8673`), inherited |
| Packets | `class_2846/2848/2849/2850` | `class_2828$class_2830` (Full), `$class_2831` (LookAndOnGround), `$class_5911` (OnGroundOnly), `$class_2829` (PositionAndOnGround) |
| Move-packet constructors | `(DDDFFZ)V`, `(FFZ)V`, `(Z)V` | `(DDDFFZZ)V`, `(FFZZ)V`, `(ZZ)V` (extra `horizontalCollision` boolean) |
| `Vec3d.getX/getY/getZ` | `method_1020..1022` (`method_1020` is `subtract`; not getters) | use public fields `x/y/z` = `field_1352/1351/1350` |
| `MinecraftClient.currentScreen` | `field_1753` (does not exist) | `field_1755` |

Also removed: unused constants that were wrong or unverifiable (`Box`, `class_258`, `Entity.distanceTo`, `getBoundingBox`, ...).

## Behavioural fix found while verifying

`KillAura` called `PlayerEntity.attack(Entity)` directly, which only runs the local attack and never tells the server.
It now calls `ClientPlayerInteractionManager.attackEntity(player, target)` (`class_636.method_2918`), which sends the
attack packet and then runs the local attack.

## Still unverified (needs the game)

- Inherited lookups: `Entity.getId` (declared on `EntityLike`), `Entity.setSprinting` via `ClientPlayerEntity`,
  `sendPacket` via `ClientPlayNetworkHandler`. JNI resolves these through superclasses/interfaces; yarn does not record
  the class relationships.
- `FullBright`: `SimpleOption.setValue` may reject values outside the option's validated range (gamma > 1) and fall
  back to the default; whether 1.21.11 allows 15.0 is unconfirmed. The usual workaround (writing the option's `value`
  field directly) is not implemented.
- `Fly`/`NoFall`/`Speed`/`Sprint`: the calls are now to real members, but their effect (and the server's reaction to
  the packets) has not been observed.
- The Fabric-intermediary assumption itself: names resolve only when the game runs under Fabric Loader.
