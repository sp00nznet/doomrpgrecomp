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


def _read_manifest_pairs(jar_path: str) -> list[tuple[str, str]]:
    """Parse META-INF/MANIFEST.MF into a list of (key, value) attribute pairs
    (continuation lines unfolded). Empty if absent/unreadable."""
    try:
        with zipfile.ZipFile(jar_path) as z:
            raw = z.read("META-INF/MANIFEST.MF").decode("utf-8", "replace")
    except (KeyError, OSError):
        return []
    lines = []
    for ln in raw.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if ln[:1] == " " and lines:          # leading space continues previous
            lines[-1] += ln[1:]
        else:
            lines.append(ln)
    pairs = []
    for ln in lines:
        if ":" in ln:
            k, v = ln.split(":", 1)
            k = k.strip()
            if k:
                pairs.append((k, v.strip()))
    return pairs


def _read_manifest_entry(jar_path: str) -> str | None:
    """Return the MIDlet-1 main class as an internal name (dots -> slashes)."""
    for k, v in _read_manifest_pairs(jar_path):
        if k.lower() == "midlet-1":
            parts = v.split(",")           # "<name>, <icon>, <class>"
            if len(parts) >= 3:
                return parts[2].strip().replace(".", "/")
    return None


def _baseline_path() -> str:
    import os
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "runtime_baseline.json")


def _merge_baseline(cg) -> int:
    """Fold the runtime-surface baseline (union of every game's runtime
    references) into this game's Codegen, so the generated header always
    declares the FULL set of runtime symbols that the hand-written runtime
    (class_meta.c, jvm_core.c, ...) references -- not just this game's subset.
    Returns how many entries were added, or -1 if no baseline file exists."""
    import os, json
    p = _baseline_path()
    if not os.path.exists(p):
        return -1
    with open(p, encoding="utf-8") as f:
        b = json.load(f)
    n = 0
    for owner, name, desc, is_static in b.get("methods", []):
        key = (owner, name, desc, bool(is_static))
        if key not in cg.ext_methods:
            cg.ext_methods.add(key); n += 1
    for cn in b.get("classes", []):
        if cn not in cg.ext_classes:
            cg.ext_classes.add(cn); n += 1
    for owner, name, desc, cstore in b.get("statics", []):
        if (owner, name, desc) not in cg.ext_statics:
            cg.ext_statics[(owner, name, desc)] = cstore; n += 1
    return n


def _populate_cg(jar):
    """Load a jar and translate every method, returning (prog, cg) with all
    external runtime references accumulated. Bodies are discarded."""
    import layout, translate
    prog = layout.load_program(jar)
    cg = translate.Codegen(prog)
    for cname in sorted(prog.classes):
        for ml in prog.classes[cname].methods:
            if ml.code is not None:
                translate.translate_method(cg, prog.classes[cname], ml)
    return prog, cg


def cmd_baseline(args) -> int:
    """Build runtime_baseline.json from the union of several games' runtime
    references. Every method registered in class_meta.c is used by at least one
    game, so this union == the full runtime surface every game's header must
    declare. Regenerate this whenever the runtime's J2ME surface changes."""
    import json
    methods, classes, statics = set(), set(), {}
    for jar in args.jars:
        _prog, cg = _populate_cg(jar)
        methods |= cg.ext_methods
        classes |= cg.ext_classes
        for k, v in cg.ext_statics.items():
            statics[k] = v
        print("  + %s: %d methods, %d classes, %d statics" %
              (jar, len(cg.ext_methods), len(cg.ext_classes), len(cg.ext_statics)))
    b = {
        "_comment": "Union of every supported game's runtime references == the "
                    "full runtime surface. Regenerate with: jrecomp baseline <jars...>",
        "methods": sorted([list(m) for m in methods]),
        "classes": sorted(classes),
        "statics": sorted([[o, n, d, s] for (o, n, d), s in statics.items()]),
    }
    with open(_baseline_path(), "w", encoding="utf-8", newline="\n") as f:
        json.dump(b, f, indent=1)
        f.write("\n")
    print("wrote %s: %d methods, %d classes, %d statics (from %d jars)" %
          (_baseline_path(), len(methods), len(classes), len(statics), len(args.jars)))
    return 0


def cmd_translate(args) -> int:
    import os
    import layout
    import mangle
    import translate
    from classfile import parse_method_descriptor

    prog = layout.load_program(args.jar)
    cg = translate.Codegen(prog)
    os.makedirs(args.out, exist_ok=True)

    # Resolve the MIDlet entry class (explicit --entry wins, else the manifest).
    entry = args.entry or _read_manifest_entry(args.jar)
    if entry and not prog.is_own(entry):
        print("warning: entry class %r not among recompiled classes" % entry)
    # Screen dimensions for the generated entry shim ("WxH").
    sw, sh = 128, 150
    if args.screen:
        try:
            sw, sh = (int(v) for v in args.screen.lower().split("x", 1))
        except ValueError:
            print("error: --screen must be WxH, e.g. 352x416"); return 2

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
    #    extern decls for every runtime symbol referenced. Fold in the runtime
    #    baseline so the header declares the full runtime surface (class_meta.c
    #    et al. reference all of it, regardless of what this game uses).
    added = _merge_baseline(cg)
    if added < 0:
        print("warning: no runtime_baseline.json -- header will only declare "
              "this game's runtime subset (run: jrecomp baseline <jars...>)")
    _emit_header(args.out, prog, cg, method_protos)

    # 3) one .c per class.
    for cname in sorted(prog.classes):
        _emit_class_c(args.out, prog, cname, per_class_bodies[cname])

    # 4) init unit (runs every <clinit>).
    _emit_init_c(args.out, prog)

    # 5) entry shim: decouples the host main.c from this game's MIDlet symbols,
    #    screen size, and JAD/manifest properties (so one runtime drives every
    #    game).
    _emit_entry_c(args.out, prog, entry, sw, sh, args.name,
                  _read_manifest_pairs(args.jar))

    n_files = len(prog.classes) + 3
    print("translated %d classes, %d methods with code -> %d files in %s"
          % (len(prog.classes), n_methods, n_files, args.out))
    print("entry MIDlet: %s   screen: %dx%d" % (entry or "(unknown)", sw, sh))
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

    # Base-offset macro for every own class, so an own class may extend another
    # own class (Doom RPG II has g->p, o->af, ...). J2ME_BASE_<cls> is where a
    # subclass's own fields begin = this class's full instance size. Macros, so
    # declaration order across the chain doesn't matter.
    L.append("")
    L.append("/* ---- own-class base offsets (enables own-extends-own) ---- */")
    for cname in sorted(prog.classes):
        L.append("#define J2ME_BASE_%s %s" %
                 (mangle.cls(cname), prog.classes[cname].instance_size_expr))

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
    L.append("/* ---- lazy class-init guards ---- */")
    for cname in sorted(prog.classes):
        if prog.classes[cname].has_clinit:
            L.append("void j_clinit_%s(void);" % mangle.cls(cname))
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
    # Only methods with a body go in the dispatch table. Abstract methods (no
    # Code) have no C definition; a concrete subclass supplies the override that
    # j_vfind finds by walking the super chain, so listing the abstract entry
    # would just reference an undefined symbol.
    table = [ml for ml in cl.methods
             if not ml.is_static and ml.name not in ("<init>", "<clinit>")
             and ml.code is not None]
    entries = ['    { %s, %s, (void*)&%s },' % (_cq(ml.name), _cq(ml.desc), ml.cfn)
               for ml in table]
    L.extend(entries if entries else ["    { 0, 0, 0 },"])
    L.append("};")
    # jclass metadata
    sup = cl.super_internal
    sup_ref = ("&%s" % mangle.class_descriptor(sup)) if sup else "0"
    L.append("const jclass %s = {" % mangle.class_descriptor(cname))
    L.append('    %s, %s, %s,' % (_cq(cname), sup_ref, cl.instance_size_expr))
    L.append("    _methods_%s, %d," % (mangle.cls(cname), len(table)))
    L.append("    0, 0,        /* interfaces (TODO) */")
    L.append("    0, 0         /* element, prim */")
    L.append("};")
    _write(out, "%s.c" % _safe(cname), "\n".join(L) + "\n")


def _emit_init_c(out, prog):
    import mangle
    # Lazy class init (JVM semantics): each class's <clinit> runs on first active
    # use (new / static field access / static call), guarded by j_clinit_<cls>().
    # Eager init-all was wrong: e.g. one game's class <clinit> reads the App
    # singleton, which only exists after new App() runs.
    L = ['/* GENERATED by jrecomp -- lazy static initializers */',
         '#include "doomrpg.h"', ""]
    for cname in sorted(prog.classes):
        cl = prog.classes[cname]
        if not cl.has_clinit:
            continue
        L.append("void j_clinit_%s(void) {" % mangle.cls(cname))
        L.append("    static char done; if (done) return; done = 1;")
        sup = cl.super_internal
        if sup and prog.is_own(sup) and prog.classes[sup].has_clinit:
            L.append("    j_clinit_%s();   /* superclass first */" % mangle.cls(sup))
        L.append("    %s();" % mangle.method(cname, "<clinit>", "()V"))
        L.append("}")
    L.append("")
    L.append("/* Classes now initialize lazily; kept for the host's call site. */")
    L.append("void j_init_all(void) { }")
    _write(out, "_init.c", "\n".join(L) + "\n")


def _emit_entry_c(out, prog, entry, screen_w, screen_h, game_name, manifest):
    """Generate game_entry.c -- the seam between the shared host runtime and this
    particular game: screen size, window title, asset-probe marker, the JAD/
    manifest properties (MIDlet.getAppProperty), and a game_run() that
    constructs+starts the game's MIDlet by its real symbol."""
    import mangle
    name = game_name or (entry or "Game")
    L = ['/* GENERATED by jrecomp -- per-game entry shim. */',
         '#include "doomrpg.h"', "",
         "/* Native screen size (host display + GameCanvas framebuffer). */",
         "int g_screen_w = %d;" % screen_w,
         "int g_screen_h = %d;" % screen_h,
         'const char *g_game_name = %s;' % _cq(name),
         "",
         "/* assets_probe() looks for this file to recognise an extracted JAR. */",
         'const char *g_asset_marker = %s;' % _cq((entry + ".class") if entry else "intro.bsp"),
         "",
         "/* MANIFEST.MF attributes, served by MIDlet.getAppProperty(). */",
         "const char *const g_manifest[] = {"]
    for k, v in manifest:
        L.append("    %s, %s," % (_cq(k), _cq(v)))
    L.append("    0, 0")
    L.append("};")
    L.append("")
    if entry and prog.is_own(entry):
        init_fn = mangle.method(entry, "<init>", "()V")
        start_fn = mangle.method(entry, "startApp", "()V")
        guard = ("    j_clinit_%s();\n" % mangle.cls(entry)) if prog.classes[entry].has_clinit else ""
        L += ["/* MIDlet lifecycle: new %s(); startApp(). */" % entry,
              "void game_run(void) {",
              guard + "    jref app = j_new(&%s);" % mangle.class_descriptor(entry),
              "    %s(app);" % init_fn,
              "    %s(app);" % start_fn,
              "}"]
    else:
        L += ["/* No usable MIDlet entry found at recompile time; host keeps the",
              "   window alive via its fallback pump loop. */",
              "void game_run(void) { }"]
    _write(out, "game_entry.c", "\n".join(L) + "\n")


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

    pt = sub.add_parser("translate", help="emit C")
    pt.add_argument("jar")
    pt.add_argument("-o", "--out", required=True)
    pt.add_argument("--screen", help="native screen size WxH (e.g. 352x416)")
    pt.add_argument("--name", help="display name for the window title")
    pt.add_argument("--entry", help="entry MIDlet class (default: from MANIFEST.MF)")
    pt.set_defaults(func=cmd_translate)

    pb = sub.add_parser("baseline",
                        help="(re)build runtime_baseline.json from several jars")
    pb.add_argument("jars", nargs="+")
    pb.set_defaults(func=cmd_baseline)

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
