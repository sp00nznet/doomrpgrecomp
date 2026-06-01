#!/usr/bin/env python3
"""
jrecomp.py -- the Doom RPG static recompiler driver.

Reads a J2ME JAR, parses every class, and (eventually) emits C. Right now it
implements the `info` command, which parses all classes and reports what's
inside -- the smoke test that proves the .class parser handles the real game.

Usage:
    python jrecomp.py info  <DoomRPG.jar>
    python jrecomp.py info  <DoomRPG.jar> --opcodes      # histogram of opcodes used
    python jrecomp.py info  <DoomRPG.jar> --api          # external classes referenced
    python jrecomp.py translate <DoomRPG.jar> -o <dir>   # (coming soon)
"""

from __future__ import annotations

import argparse
import sys
import zipfile
from collections import Counter

import classfile
from classfile import (CONSTANT_Class, CONSTANT_Fieldref, CONSTANT_Methodref,
                       CONSTANT_InterfaceMethodref)
import opcodes


def _load_classes(jar_path: str) -> dict[str, classfile.ClassFile]:
    classes = {}
    with zipfile.ZipFile(jar_path) as z:
        for name in z.namelist():
            if not name.endswith(".class"):
                continue
            cf = classfile.parse(z.read(name))
            classes[cf.this_class] = cf
    return classes


def cmd_info(args) -> int:
    classes = _load_classes(args.jar)
    total_methods = total_fields = total_code = 0
    op_hist: Counter = Counter()
    api_classes: set[str] = set()
    api_methods: set[tuple[str, str, str]] = set()
    unknown_ops: Counter = Counter()

    own_names = set(classes.keys())

    print("=" * 70)
    print("  Doom RPG :: class inventory")
    print("=" * 70)
    for name in sorted(classes):
        cf = classes[name]
        ver = "%d.%d" % (cf.major_version, cf.minor_version)
        nm = len(cf.methods)
        nf = len(cf.fields)
        total_methods += nm
        total_fields += nf
        sup = cf.super_class or "-"
        kind = "interface" if cf.is_interface else "class"
        print(f"  {name:<14} {kind:<9} v{ver:<6} super={sup:<24} "
              f"methods={nm:<3} fields={nf}")

        for m in cf.methods:
            if m.code is None:
                continue
            total_code += 1
            for op, _operands, _length in opcodes.iterate(m.code.code):
                mn = opcodes.NAME.get(op)
                if mn is None:
                    unknown_ops[op] += 1
                else:
                    op_hist[mn] += 1

        # external API references
        cp = cf.constant_pool
        for e in cp.entries:
            if e is None:
                continue
            if e.tag == CONSTANT_Class:
                cn = cp.utf8(e.name_index)
                if not cn.startswith("[") and cn not in own_names:
                    api_classes.add(cn)
            elif e.tag in (CONSTANT_Methodref, CONSTANT_InterfaceMethodref):
                owner, mname, desc = cp.ref(cp.entries.index(e))
                if owner not in own_names and not owner.startswith("["):
                    api_methods.add((owner, mname, desc))

    print("-" * 70)
    print(f"  classes={len(classes)}  methods={total_methods}  "
          f"(with code: {total_code})  fields={total_fields}")
    print(f"  distinct opcodes used: {len(op_hist)}")
    if unknown_ops:
        print(f"  !! UNKNOWN opcodes: "
              f"{', '.join('0x%02x x%d' % (k, v) for k, v in unknown_ops.items())}")
    else:
        print("  all opcodes recognized by the translator's opcode table [OK]")

    if args.opcodes:
        print("-" * 70)
        print("  opcode histogram (most frequent first):")
        for mn, c in op_hist.most_common():
            print(f"    {mn:<18} {c}")

    if args.api:
        print("-" * 70)
        print("  external classes referenced (the runtime contract):")
        for cn in sorted(api_classes):
            print(f"    {cn}")
        print("-" * 70)
        print(f"  external methods referenced: {len(api_methods)}")
        for owner, mname, desc in sorted(api_methods):
            print(f"    {owner}.{mname}{desc}")

    return 0 if not unknown_ops else 1


def cmd_translate(args) -> int:
    print("translate: not implemented yet -- parser/opcode infra landing first.",
          file=sys.stderr)
    return 2


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="jrecomp", description="Doom RPG static recompiler")
    sub = p.add_subparsers(dest="cmd", required=True)

    pi = sub.add_parser("info", help="parse all classes and report")
    pi.add_argument("jar")
    pi.add_argument("--opcodes", action="store_true", help="show opcode histogram")
    pi.add_argument("--api", action="store_true", help="show external API references")
    pi.set_defaults(func=cmd_info)

    pt = sub.add_parser("translate", help="emit C (coming soon)")
    pt.add_argument("jar")
    pt.add_argument("-o", "--out", required=True)
    pt.set_defaults(func=cmd_translate)

    # The Windows console defaults to cp1252; force UTF-8 so box-drawing and
    # any non-ASCII identifiers print cleanly.
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass

    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
