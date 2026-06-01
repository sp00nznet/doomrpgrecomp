"""
layout.py -- object layout, symbol resolution, and class metadata.

Builds a Program: the registry of all recompiled classes plus everything the
translator needs to emit correct C:

  * instance field byte offsets (as C expressions: J2ME_BASE_<super> + N)
  * static field C globals (typed by storage type)
  * method C function names + computational signatures
  * per-class method table + jclass metadata for runtime dispatch

Assumes (verified for Doom RPG) that game classes never extend each other --
every super is a runtime class (Object / MIDlet / GameCanvas). That keeps field
offsets to a single base term.
"""

from __future__ import annotations

from dataclasses import dataclass, field as dc_field

import classfile
import mangle
from classfile import (ACC_STATIC, parse_method_descriptor)


# JVM type char -> (C computational type, C storage type, storage size, J-macro)
# computational type is what lives on the operand stack / in params.
_PRIM = {
    "I": ("jint", "jint", 4, "J_I"),
    "Z": ("jint", "jbyte", 1, "J_Z"),
    "B": ("jint", "jbyte", 1, "J_B"),
    "C": ("jint", "jchar", 2, "J_C"),
    "S": ("jint", "jshort", 2, "J_S"),
    "J": ("jlong", "jlong", 8, "J_J"),
    "F": ("jfloat", "jfloat", 4, "J_F"),
    "D": ("jdouble", "jdouble", 8, "J_D"),
    "V": ("void", "void", 0, None),
}


def comp_ctype(desc_type: str) -> str:
    """Computational C type for a field/param/return type string."""
    if desc_type[0] in ("L", "["):
        return "jref"
    return _PRIM[desc_type][0]


def storage(desc_type: str):
    """(c_storage_type, size_bytes, access_macro) for a field's stored form."""
    if desc_type[0] in ("L", "["):
        return ("jref", 8, "J_REF")   # pointer; size used only for offset math
    ct, st, sz, mac = _PRIM[desc_type]
    return (st, sz, mac)


def type_tag(desc_type: str) -> str:
    """One-char operand-stack tag: i/l/f/d/a."""
    if desc_type[0] in ("L", "["):
        return "a"
    return {"I": "i", "Z": "i", "B": "i", "C": "i", "S": "i",
            "J": "l", "F": "f", "D": "d"}[desc_type]


@dataclass
class FieldLayout:
    owner: str
    name: str
    desc: str
    access: int
    member: str             # mangled C identifier
    is_static: bool
    # instance only:
    off_expr: str = ""      # C expression for byte offset
    c_storage: str = ""     # storage C type
    access_macro: str = ""  # J_I / J_REF / ...


@dataclass
class MethodLayout:
    owner: str
    name: str
    desc: str
    access: int
    cfn: str                # mangled C function name
    params: list[str]       # raw JVM param type strings
    ret: str                # raw JVM return type string
    code: object            # classfile.CodeAttribute or None

    @property
    def is_static(self) -> bool:
        return bool(self.access & ACC_STATIC)

    @property
    def is_clinit(self) -> bool:
        return self.name == "<clinit>"

    def c_signature(self, name: str | None = None) -> str:
        """Render the C prototype text (without trailing ';')."""
        ret_c = comp_ctype(self.ret)
        ps = []
        if not self.is_static:
            ps.append("jref this_")
        for i, p in enumerate(self.params):
            ps.append("%s a%d" % (comp_ctype(p), i))
        if not ps:
            ps.append("void")
        return "%s %s(%s)" % (ret_c, name or self.cfn, ", ".join(ps))

    def fnptr_cast(self) -> str:
        """A C cast turning a void* into this method's function-pointer type."""
        ret_c = comp_ctype(self.ret)
        ps = []
        if not self.is_static:
            ps.append("jref")
        for p in self.params:
            ps.append(comp_ctype(p))
        if not ps:
            ps.append("void")
        return "%s (*)(%s)" % (ret_c, ", ".join(ps))


@dataclass
class ClassLayout:
    cf: classfile.ClassFile
    name: str
    super_internal: str | None
    base_macro: str
    instance_fields: list[FieldLayout] = dc_field(default_factory=list)
    static_fields: list[FieldLayout] = dc_field(default_factory=list)
    methods: list[MethodLayout] = dc_field(default_factory=list)
    instance_size_expr: str = ""
    own_field_index: dict = dc_field(default_factory=dict)  # (name,desc)->FieldLayout

    @property
    def has_clinit(self) -> bool:
        return any(m.is_clinit for m in self.methods)


def _align(off: int, a: int) -> int:
    return (off + a - 1) & ~(a - 1)


class Program:
    def __init__(self):
        self.classes: dict[str, ClassLayout] = {}

    @property
    def own_names(self) -> set[str]:
        return set(self.classes)

    def is_own(self, internal_name: str) -> bool:
        return internal_name in self.classes

    def add(self, cf: classfile.ClassFile) -> ClassLayout:
        sup = cf.super_class
        base_macro = "J2ME_BASE_" + mangle.cls(sup) if sup else "0"
        cl = ClassLayout(cf=cf, name=cf.this_class, super_internal=sup,
                         base_macro=base_macro)

        off = 0  # bytes past the base region
        for f in cf.fields:
            is_static = bool(f.access_flags & ACC_STATIC)
            if is_static:
                cstore, _sz, _mac = storage(f.descriptor)
                fl = FieldLayout(owner=cf.this_class, name=f.name, desc=f.descriptor,
                                 access=f.access_flags,
                                 member=mangle.static_field(cf.this_class, f.name, f.descriptor),
                                 is_static=True, c_storage=cstore)
                cl.static_fields.append(fl)
            else:
                cstore, sz, mac = storage(f.descriptor)
                off = _align(off, sz if sz else 1)
                off_expr = "(%s + %d)" % (base_macro, off)
                fl = FieldLayout(owner=cf.this_class, name=f.name, desc=f.descriptor,
                                 access=f.access_flags,
                                 member=mangle.field_member(cf.this_class, f.name, f.descriptor),
                                 is_static=False, off_expr=off_expr,
                                 c_storage=cstore, access_macro=mac)
                cl.instance_fields.append(fl)
                cl.own_field_index[(f.name, f.descriptor)] = fl
                off += sz

        off = _align(off, 8)
        cl.instance_size_expr = "(%s + %d)" % (base_macro, off)

        for m in cf.methods:
            params, ret = parse_method_descriptor(m.descriptor)
            ml = MethodLayout(owner=cf.this_class, name=m.name, desc=m.descriptor,
                              access=m.access_flags,
                              cfn=mangle.method(cf.this_class, m.name, m.descriptor),
                              params=params, ret=ret, code=m.code)
            cl.methods.append(ml)

        self.classes[cf.this_class] = cl
        return cl

    # -- symbol resolution used by the translator --------------------------
    def resolve_instance_field(self, owner: str, name: str, desc: str) -> FieldLayout:
        cl = self.classes.get(owner)
        if cl is not None:
            fl = cl.own_field_index.get((name, desc))
            if fl is not None:
                return fl
        # Not an own field (or owner is a runtime class). Game code is not
        # expected to touch runtime base fields; surface it loudly if it does.
        raise KeyError("instance field %s.%s:%s not found in recompiled classes"
                       % (owner, name, desc))

    def static_field_member(self, owner: str, name: str, desc: str) -> tuple[str, bool]:
        """Return (c_global_name, is_own). External statics get an extern decl."""
        return (mangle.static_field(owner, name, desc), self.is_own(owner))

    def method_layout(self, owner: str, name: str, desc: str) -> MethodLayout | None:
        cl = self.classes.get(owner)
        if cl is None:
            return None
        for m in cl.methods:
            if m.name == name and m.desc == desc:
                return m
        return None


def load_program(jar_path: str) -> Program:
    import zipfile
    prog = Program()
    with zipfile.ZipFile(jar_path) as z:
        for n in sorted(z.namelist()):
            if n.endswith(".class"):
                prog.add(classfile.parse(z.read(n)))
    return prog
