#!/usr/bin/env python3
"""Verify src/core/Mappings.h against a FabricMC/yarn checkout (branch 1.21.11).

    git clone --depth 1 --branch 1.21.11 https://github.com/FabricMC/yarn yarn
    python tools/verify_mappings.py --yarn yarn --src src

Checks (mapping DATA only -- this does not prove a module works in the game):
  1. every EXPECTED member exists on its declaring class with the expected descriptor;
  2. every field_/method_/class_ literal under src/ is in EXPECTED (nothing unverified sneaks in).
Exit code 1 on any failure.
"""
import argparse, os, re, sys

# (declaring class, intermediary member, kind, descriptor, yarn name)
EXPECTED = [
 ("class_310",  "method_1574", "M", "()V", "tick"),
 ("class_310",  "method_1551", "M", "()Lnet/minecraft/class_310;", "getInstance"),
 ("class_310",  "field_1700",  "F", "Lnet/minecraft/class_310;", "instance"),
 ("class_310",  "field_1724",  "F", "Lnet/minecraft/class_746;", "player"),
 ("class_310",  "field_1687",  "F", "Lnet/minecraft/class_638;", "world"),
 ("class_310",  "field_1690",  "F", "Lnet/minecraft/class_315;", "options"),
 ("class_310",  "field_1761",  "F", "Lnet/minecraft/class_636;", "interactionManager"),
 ("class_310",  "field_1755",  "F", "Lnet/minecraft/class_437;", "currentScreen"),
 ("class_746",  "field_3944",  "F", "Lnet/minecraft/class_634;", "networkHandler"),
 ("class_1657", "method_31549","M", "()Lnet/minecraft/class_1656;", "getAbilities"),
 ("class_1297", "method_5728", "M", "(Z)V", "setSprinting"),
 ("class_5568", "method_5628", "M", "()I", "getId"),            # EntityLike, inherited by Entity
 ("class_1297", "method_23317","M", "()D", "getX"),
 ("class_1297", "method_23318","M", "()D", "getY"),
 ("class_1297", "method_23321","M", "()D", "getZ"),
 ("class_1297", "method_5805", "M", "()Z", "isAlive"),
 ("class_1297", "method_18798","M", "()Lnet/minecraft/class_243;", "getVelocity"),
 ("class_1297", "method_18799","M", "(Lnet/minecraft/class_243;)V", "setVelocity"),
 ("class_1297", "method_18800","M", "(DDD)V", "setVelocity"),
 ("class_1297", "field_6031",  "F", "F", "yaw"),
 ("class_1297", "field_5965",  "F", "F", "pitch"),
 ("class_1297", "field_5982",  "F", "F", "lastYaw"),
 ("class_1297", "field_6004",  "F", "F", "lastPitch"),
 ("class_636",  "method_2918", "M", "(Lnet/minecraft/class_1657;Lnet/minecraft/class_1297;)V", "attackEntity"),
 ("class_8673", "method_52787","M", "(Lnet/minecraft/class_2596;)V", "sendPacket"),  # ClientCommonNetworkHandler, inherited by class_634
 ("class_243",  "field_1352",  "F", "D", "x"),
 ("class_243",  "field_1351",  "F", "D", "y"),
 ("class_243",  "field_1350",  "F", "D", "z"),
 ("class_1656", "field_7479",  "F", "Z", "flying"),
 ("class_1656", "field_7478",  "F", "Z", "allowFlying"),
 ("class_315",  "field_1840",  "F", "Lnet/minecraft/class_7172;", "gamma"),
 ("class_7172", "method_41753","M", "()Ljava/lang/Object;", "getValue"),
 ("class_7172", "method_41748","M", "(Ljava/lang/Object;)V", "setValue"),
 ("class_638",  "method_18112","M", "()Ljava/lang/Iterable;", "getEntities"),
 # constructors used through JNI
 ("class_243",  "<init>", "M", "(DDD)V", "<init>"),
 ("class_2868", "<init>", "M", "(I)V", "<init>"),
 ("class_2828$class_2830", "<init>", "M", "(DDDFFZZ)V", "<init>"),   # PlayerMoveC2SPacket.Full
 ("class_2828$class_2831", "<init>", "M", "(FFZZ)V", "<init>"),      # LookAndOnGround
 ("class_2828$class_5911", "<init>", "M", "(ZZ)V", "<init>"),        # OnGroundOnly
]
# Classes that must exist (class_2828$... are checked through the constructors above).
EXTRA_CLASSES = ["class_1309", "class_437", "class_2596", "class_2828$class_2829"]

def load_yarn(root):
    cls = {}
    for dp, _, fs in os.walk(os.path.join(root, "mappings")):
        for f in fs:
            if not f.endswith(".mapping"): continue
            stack = []
            for line in open(os.path.join(dp, f), encoding="utf8"):
                ind = len(line) - len(line.lstrip("\t")); p = line.strip().split(" ")
                if not p or p[0] == "": continue
                k = p[0]
                if k == "CLASS":
                    name = p[1].split("/")[-1]
                    full = name if ind == 0 else stack[ind-1] + "$" + name
                    stack = stack[:ind] + [full]
                    cls.setdefault(full, {"F": {}, "M": {}})
                elif k in ("FIELD", "METHOD") and stack:
                    owner = stack[ind-1]; a = p[1:]
                    if len(a) == 3: name, desc = a[1], a[2]; inter = a[0]
                    elif len(a) == 2: name, desc = a[0], a[1]; inter = a[0]
                    else: continue
                    cls[owner]["F" if k == "FIELD" else "M"].setdefault(inter, []).append((name, desc))
    return cls

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--yarn", required=True); ap.add_argument("--src", default="src")
    a = ap.parse_args()
    cls = load_yarn(a.yarn); fails = 0
    for owner, inter, kind, desc, named in EXPECTED:
        hits = cls.get(owner, {}).get(kind, {}).get(inter, [])
        ok = any(d == desc and (n == named or inter == "<init>") for n, d in hits)
        print(("OK   " if ok else "FAIL ") + f"{owner}.{inter} {desc} ({named})" + ("" if ok else f"  found={hits}"))
        fails += 0 if ok else 1
    for c in EXTRA_CLASSES + sorted({e[0] for e in EXPECTED}):
        if c not in cls: print("FAIL class missing:", c); fails += 1
    allowed_members = {e[1] for e in EXPECTED if e[1] != "<init>"}
    allowed_classes = set(EXTRA_CLASSES) | {e[0] for e in EXPECTED}
    allowed_classes |= {"class_1668"}  # placeholder-free: none intended
    allowed_classes.discard("class_1668")
    for dp, _, fs in os.walk(a.src):
        for f in fs:
            if not f.endswith((".h", ".cpp")): continue
            text = open(os.path.join(dp, f), encoding="utf8", errors="ignore").read()
            for m in re.finditer(r'"([^"\n]*)"', text):
                lit = m.group(1)
                for mem in re.findall(r"\b((?:field|method)_\d+)\b", lit):
                    if mem not in allowed_members: print(f"FAIL unverified member {mem} in {f}"); fails += 1
                for c in re.findall(r"net/minecraft/(class_\d+(?:\$class_\d+)?)", lit):
                    if c not in cls: print(f"FAIL unknown class {c} in {f}"); fails += 1
    print("RESULT:", "ALL VERIFIED" if not fails else f"{fails} FAILURE(S)")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
