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
    import os
    import layout
    import mangle
    import translate
    from classfile import parse_method_descriptor

    prog = layout.load_program(args.jar)
    cg = translate.Codegen(prog)
    os.makedirs(args.out, exist_ok=True)

    # 1) translate every method body, collecting external references.
    per_class_bodies: dict[str, list[str]] = {}
    method_protos: list[str] = []
    n_methods = 0
    for cname in sorted(prog.classes):
        cl = prog.classes[cname]
        bodies = []
        for ml in cl.methods:
            method_protos.append("%s;" % ml.c_signature())
            if ml.code is None:
                continue
            bodies.append(translate.translate_method(cg, cl, ml))
            n_methods += 1
        per_class_bodies[cname] = bodies

    # 2) aggregate header: types, own class metadata + statics + protos, and
    #    extern decls for every runtime symbol the game references.
    _emit_header(args.out, prog, cg, method_protos)

    # 3) one .c per class.
    for cname in sorted(prog.classes):
        _emit_class_c(args.out, prog, cname, per_class_bodies[cname])

    # 4) init unit (runs every <clinit>).
    _emit_init_c(args.out, prog)

    n_files = len(prog.classes) + 2
    print("translated %d classes, %d methods with code -> %d files in %s"
          % (len(prog.classes), n_methods, n_files, args.out))
    print("external runtime symbols referenced: %d methods, %d classes, %d statics"
          % (len(cg.ext_methods), len(cg.ext_classes), len(cg.ext_statics)))
    return 0


def _emit_header(out, prog, cg, method_protos):
    import mangle
    import layout
    from classfile import parse_method_descriptor
    L = ["/* GENERATED by jrecomp -- do not edit. */",
         "#ifndef DOOMRPG_GENERATED_H", "#define DOOMRPG_GENERATED_H",
         '#include "j2me/jvm.h"', "", "/* ---- class metadata ---- */"]
    for cname in sorted(prog.classes):
        L.append("extern const jclass %s;" % mangle.class_descriptor(cname))
    # runtime classes referenced directly (new/checkcast/...) plus every
    # external superclass (needed for each own class's jclass.super pointer).
    runtime_classes = set(cg.ext_classes)
    for cname in prog.classes:
        sup = prog.classes[cname].super_internal
        if sup and not prog.is_own(sup):
            runtime_classes.add(sup)
    for cname in sorted(runtime_classes):
        L.append("extern const jclass %s; /* runtime */" % mangle.class_descriptor(cname))

    L.append("")
    L.append("/* ---- static fields (own) ---- */")
    for cname in sorted(prog.classes):
        for f in prog.classes[cname].static_fields:
            L.append("extern %s %s;" % (f.c_storage, f.member))
    L.append("/* ---- static fields (runtime) ---- */")
    for (owner, name, desc), cstore in sorted(cg.ext_statics.items()):
        L.append("extern %s %s; /* %s.%s */" % (cstore, mangle.static_field(owner, name, desc), owner, name))

    L.append("")
    L.append("/* ---- own method prototypes ---- */")
    L.extend(sorted(set(method_protos)))

    L.append("")
    L.append("/* ---- runtime method prototypes ---- */")
    seen = set()
    for (owner, name, desc, is_static) in sorted(cg.ext_methods):
        params, ret = parse_method_descriptor(desc)
        fn = mangle.method(owner, name, desc)
        if fn in seen:
            continue
        seen.add(fn)
        ps = ([] if is_static else ["jref this_"])
        for i, p in enumerate(params):
            ps.append("%s a%d" % (layout.comp_ctype(p), i))
        if not ps:
            ps.append("void")
        L.append("%s %s(%s); /* %s.%s%s */" %
                 (layout.comp_ctype(ret), fn, ", ".join(ps), owner, name, desc))

    L.append("")
    L.append("void j_init_all(void);")
    L.append("#endif")
    _write(out, "doomrpg.h", "\n".join(L) + "\n")


def _emit_class_c(out, prog, cname, bodies):
    import mangle
    cl = prog.classes[cname]
    L = ['/* GENERATED by jrecomp -- class %s */' % cname,
         '#include "doomrpg.h"', ""]
    # static field definitions
    for f in cl.static_fields:
        L.append("%s %s;" % (f.c_storage, f.member))
    if cl.static_fields:
        L.append("")
    # method bodies
    for b in bodies:
        L.append(b)
        L.append("")
    # method table for virtual/interface dispatch (instance, non-init methods)
    L.append("static const jmethod _methods_%s[] = {" % mangle.cls(cname))
    entries = []
    for ml in cl.methods:
        if ml.is_static or ml.name in ("<init>", "<clinit>"):
            continue
        entries.append('    { %s, %s, (void*)&%s },' %
                        (_cq(ml.name), _cq(ml.desc), ml.cfn))
    L.extend(entries if entries else ["    { 0, 0, 0 },"])
    L.append("};")
    # jclass metadata
    sup = cl.super_internal
    sup_ref = ("&%s" % mangle.class_descriptor(sup)) if sup else "0"
    L.append("const jclass %s = {" % mangle.class_descriptor(cname))
    L.append('    %s, %s, %s,' % (_cq(cname), sup_ref, cl.instance_size_expr))
    L.append("    _methods_%s, %d," % (mangle.cls(cname),
             len([m for m in cl.methods if not m.is_static and m.name not in ("<init>", "<clinit>")])))
    L.append("    0, 0,        /* interfaces (TODO) */")
    L.append("    0, 0         /* element, prim */")
    L.append("};")
    _write(out, "%s.c" % _safe(cname), "\n".join(L) + "\n")


def _emit_init_c(out, prog):
    import mangle
    L = ['/* GENERATED by jrecomp -- static initializers */',
         '#include "doomrpg.h"', "", "void j_init_all(void) {"]
    for cname in sorted(prog.classes):
        if prog.classes[cname].has_clinit:
            L.append("    %s();" % mangle.method(cname, "<clinit>", "()V"))
    L.append("}")
    _write(out, "_init.c", "\n".join(L) + "\n")


def _safe(cname):
    import mangle
    return mangle.cls(cname)


def _cq(s):
    from translate import _cstr
    return _cstr(s)


def _write(out, name, text):
    import os
    with open(os.path.join(out, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


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
