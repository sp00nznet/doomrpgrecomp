"""
classfile.py -- a complete JVM .class file parser (JVMS Ch. 4).

Doom RPG's classes are compiled for CLDC-1.0 / MIDP-2.0, which means an ancient
class file version (typically major 46/47/48) using only the classic constant
pool tags -- no invokedynamic, no method handles, no constant-dynamic. We still
parse the full classic format so the recompiler has everything it needs:
constant pool, fields, methods, and the Code attribute (bytecode + exception
table) for every method.

A key wrinkle for obfuscated J2ME jars: fields and methods are routinely
overloaded by *descriptor* under a single name (six fields all named "a" with
different types). So callers must key every symbol on (owner, name, descriptor),
never on name alone. This parser preserves descriptors verbatim and never
collapses by name.

No third-party dependencies; standard library only.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Optional


# ---------------------------------------------------------------------------
# Constant pool tags (JVMS Table 4.4-A)
# ---------------------------------------------------------------------------
CONSTANT_Utf8 = 1
CONSTANT_Integer = 3
CONSTANT_Float = 4
CONSTANT_Long = 5
CONSTANT_Double = 6
CONSTANT_Class = 7
CONSTANT_String = 8
CONSTANT_Fieldref = 9
CONSTANT_Methodref = 10
CONSTANT_InterfaceMethodref = 11
CONSTANT_NameAndType = 12
CONSTANT_MethodHandle = 15
CONSTANT_MethodType = 16
CONSTANT_Dynamic = 17
CONSTANT_InvokeDynamic = 18
CONSTANT_Module = 19
CONSTANT_Package = 20

# Access flags (JVMS Tables 4.1-B, 4.5-A, 4.6-A)
ACC_PUBLIC = 0x0001
ACC_PRIVATE = 0x0002
ACC_PROTECTED = 0x0004
ACC_STATIC = 0x0008
ACC_FINAL = 0x0010
ACC_SUPER = 0x0020          # class
ACC_SYNCHRONIZED = 0x0020   # method
ACC_VOLATILE = 0x0040       # field
ACC_BRIDGE = 0x0040         # method
ACC_TRANSIENT = 0x0080      # field
ACC_VARARGS = 0x0080        # method
ACC_NATIVE = 0x0100
ACC_INTERFACE = 0x0200
ACC_ABSTRACT = 0x0400
ACC_STRICT = 0x0800
ACC_SYNTHETIC = 0x1000
ACC_ANNOTATION = 0x2000
ACC_ENUM = 0x4000


class ClassFormatError(Exception):
    """Raised when a .class file is malformed or uses an unsupported feature."""


# ---------------------------------------------------------------------------
# Byte reader
# ---------------------------------------------------------------------------
class _Reader:
    __slots__ = ("buf", "pos")

    def __init__(self, buf: bytes):
        self.buf = buf
        self.pos = 0

    def u1(self) -> int:
        v = self.buf[self.pos]
        self.pos += 1
        return v

    def u2(self) -> int:
        v = struct.unpack_from(">H", self.buf, self.pos)[0]
        self.pos += 2
        return v

    def u4(self) -> int:
        v = struct.unpack_from(">I", self.buf, self.pos)[0]
        self.pos += 4
        return v

    def s4(self) -> int:
        v = struct.unpack_from(">i", self.buf, self.pos)[0]
        self.pos += 4
        return v

    def s8(self) -> int:
        v = struct.unpack_from(">q", self.buf, self.pos)[0]
        self.pos += 8
        return v

    def f4(self) -> float:
        v = struct.unpack_from(">f", self.buf, self.pos)[0]
        self.pos += 4
        return v

    def f8(self) -> float:
        v = struct.unpack_from(">d", self.buf, self.pos)[0]
        self.pos += 8
        return v

    def bytes(self, n: int) -> bytes:
        v = self.buf[self.pos:self.pos + n]
        if len(v) != n:
            raise ClassFormatError("unexpected EOF reading %d bytes" % n)
        self.pos += n
        return v


# ---------------------------------------------------------------------------
# Constant pool
# ---------------------------------------------------------------------------
@dataclass
class CpInfo:
    tag: int
    # union of fields, by tag:
    text: Optional[str] = None          # Utf8
    value: object = None                # Integer/Float/Long/Double
    name_index: int = 0                 # Class, Module, Package
    string_index: int = 0               # String, MethodType
    class_index: int = 0                # Fieldref/Methodref/InterfaceMethodref
    name_and_type_index: int = 0        # Fieldref/Methodref/InterfaceMethodref, Dynamic
    descriptor_index: int = 0           # NameAndType
    reference_kind: int = 0             # MethodHandle
    reference_index: int = 0            # MethodHandle


class ConstantPool:
    """1-indexed constant pool. Long/Double occupy two slots (JVMS 4.4.5)."""

    def __init__(self, entries: list[Optional[CpInfo]]):
        self.entries = entries  # index 0 is None; longs/doubles leave a None hole

    def __len__(self) -> int:
        return len(self.entries)

    def get(self, i: int) -> CpInfo:
        e = self.entries[i]
        if e is None:
            raise ClassFormatError("constant pool index %d is invalid/empty" % i)
        return e

    def utf8(self, i: int) -> str:
        e = self.get(i)
        if e.tag != CONSTANT_Utf8:
            raise ClassFormatError("cp[%d] expected Utf8, got tag %d" % (i, e.tag))
        return e.text

    def class_name(self, i: int) -> str:
        e = self.get(i)
        if e.tag != CONSTANT_Class:
            raise ClassFormatError("cp[%d] expected Class, got tag %d" % (i, e.tag))
        return self.utf8(e.name_index)

    def name_and_type(self, i: int) -> tuple[str, str]:
        e = self.get(i)
        if e.tag != CONSTANT_NameAndType:
            raise ClassFormatError("cp[%d] expected NameAndType, got tag %d" % (i, e.tag))
        return self.utf8(e.name_index), self.utf8(e.descriptor_index)

    def ref(self, i: int) -> tuple[str, str, str]:
        """Return (owner_class, name, descriptor) for a Field/Method/InterfaceMethod ref."""
        e = self.get(i)
        if e.tag not in (CONSTANT_Fieldref, CONSTANT_Methodref,
                         CONSTANT_InterfaceMethodref):
            raise ClassFormatError("cp[%d] expected a ref, got tag %d" % (i, e.tag))
        owner = self.class_name(e.class_index)
        name, desc = self.name_and_type(e.name_and_type_index)
        return owner, name, desc

    def string(self, i: int) -> str:
        e = self.get(i)
        if e.tag != CONSTANT_String:
            raise ClassFormatError("cp[%d] expected String, got tag %d" % (i, e.tag))
        return self.utf8(e.string_index)

    def ldc_value(self, i: int):
        """Decode the value an ldc/ldc_w/ldc2_w pushes. Returns (kind, value)."""
        e = self.get(i)
        if e.tag == CONSTANT_Integer:
            return ("int", e.value)
        if e.tag == CONSTANT_Float:
            return ("float", e.value)
        if e.tag == CONSTANT_Long:
            return ("long", e.value)
        if e.tag == CONSTANT_Double:
            return ("double", e.value)
        if e.tag == CONSTANT_String:
            return ("String", self.utf8(e.string_index))
        if e.tag == CONSTANT_Class:
            return ("Class", self.utf8(e.name_index))
        raise ClassFormatError("cp[%d] not a loadable constant (tag %d)" % (i, e.tag))


def _parse_constant_pool(r: _Reader) -> ConstantPool:
    count = r.u2()
    entries: list[Optional[CpInfo]] = [None] * count
    i = 1
    while i < count:
        tag = r.u1()
        info = CpInfo(tag=tag)
        if tag == CONSTANT_Utf8:
            n = r.u2()
            raw = r.bytes(n)
            info.text = _decode_modified_utf8(raw)
        elif tag == CONSTANT_Integer:
            info.value = r.s4()
        elif tag == CONSTANT_Float:
            info.value = r.f4()
        elif tag == CONSTANT_Long:
            info.value = r.s8()
        elif tag == CONSTANT_Double:
            info.value = r.f8()
        elif tag in (CONSTANT_Class, CONSTANT_Module, CONSTANT_Package):
            info.name_index = r.u2()
        elif tag in (CONSTANT_String, CONSTANT_MethodType):
            info.string_index = r.u2()
        elif tag in (CONSTANT_Fieldref, CONSTANT_Methodref,
                     CONSTANT_InterfaceMethodref, CONSTANT_Dynamic,
                     CONSTANT_InvokeDynamic):
            info.class_index = r.u2()             # or bootstrap idx for (Invoke)Dynamic
            info.name_and_type_index = r.u2()
        elif tag == CONSTANT_NameAndType:
            info.name_index = r.u2()
            info.descriptor_index = r.u2()
        elif tag == CONSTANT_MethodHandle:
            info.reference_kind = r.u1()
            info.reference_index = r.u2()
        else:
            raise ClassFormatError("unknown constant pool tag %d at #%d" % (tag, i))
        entries[i] = info
        # Long and Double take two pool slots.
        if tag in (CONSTANT_Long, CONSTANT_Double):
            i += 2
        else:
            i += 1
    return ConstantPool(entries)


def _decode_modified_utf8(raw: bytes) -> str:
    """Decode JVM 'modified UTF-8' (JVMS 4.4.7): 0x00 is two bytes, plus a
    six-byte form for supplementary chars. CESU-8 covers the surrogate case;
    we handle the embedded-null special case explicitly."""
    try:
        # Python's 'utf-8' rejects the 0xC0 0x80 encoding of NUL and CESU-8
        # surrogate pairs, so go through the dedicated codec when present.
        if b"\xc0\x80" in raw:
            raise UnicodeDecodeError("utf-8", raw, 0, 1, "modified")
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        out = []
        i, n = 0, len(raw)
        while i < n:
            b = raw[i]
            if b == 0xC0 and i + 1 < n and raw[i + 1] == 0x80:
                out.append("\x00")
                i += 2
            elif b < 0x80:
                out.append(chr(b))
                i += 1
            elif (b & 0xE0) == 0xC0:
                c = ((b & 0x1F) << 6) | (raw[i + 1] & 0x3F)
                out.append(chr(c))
                i += 2
            elif (b & 0xF0) == 0xE0:
                c = ((b & 0x0F) << 12) | ((raw[i + 1] & 0x3F) << 6) | (raw[i + 2] & 0x3F)
                out.append(chr(c))
                i += 3
            else:
                out.append(chr(b))
                i += 1
        return "".join(out)


# ---------------------------------------------------------------------------
# Attributes, fields, methods
# ---------------------------------------------------------------------------
@dataclass
class Attribute:
    name: str
    data: bytes


@dataclass
class ExceptionTableEntry:
    start_pc: int
    end_pc: int
    handler_pc: int
    catch_type: int          # cp index of a CONSTANT_Class, or 0 for "any" (finally)


@dataclass
class CodeAttribute:
    max_stack: int
    max_locals: int
    code: bytes
    exception_table: list[ExceptionTableEntry]
    attributes: list[Attribute]


@dataclass
class Member:
    """A field or method."""
    access_flags: int
    name: str
    descriptor: str
    attributes: list[Attribute]
    code: Optional[CodeAttribute] = None      # methods only

    @property
    def is_static(self) -> bool:
        return bool(self.access_flags & ACC_STATIC)

    @property
    def is_native(self) -> bool:
        return bool(self.access_flags & ACC_NATIVE)

    @property
    def is_abstract(self) -> bool:
        return bool(self.access_flags & ACC_ABSTRACT)


@dataclass
class ClassFile:
    minor_version: int
    major_version: int
    constant_pool: ConstantPool
    access_flags: int
    this_class: str
    super_class: Optional[str]
    interfaces: list[str]
    fields: list[Member]
    methods: list[Member]
    attributes: list[Attribute]
    source_name: Optional[str] = None

    @property
    def is_interface(self) -> bool:
        return bool(self.access_flags & ACC_INTERFACE)


def _parse_attributes(r: _Reader, cp: ConstantPool) -> list[Attribute]:
    count = r.u2()
    out = []
    for _ in range(count):
        name = cp.utf8(r.u2())
        length = r.u4()
        data = r.bytes(length)
        out.append(Attribute(name=name, data=data))
    return out


def _parse_code_attribute(data: bytes, cp: ConstantPool) -> CodeAttribute:
    r = _Reader(data)
    max_stack = r.u2()
    max_locals = r.u2()
    code_len = r.u4()
    code = r.bytes(code_len)
    n_exc = r.u2()
    exc = []
    for _ in range(n_exc):
        exc.append(ExceptionTableEntry(
            start_pc=r.u2(), end_pc=r.u2(),
            handler_pc=r.u2(), catch_type=r.u2()))
    attrs = _parse_attributes(r, cp)
    return CodeAttribute(max_stack, max_locals, code, exc, attrs)


def _parse_members(r: _Reader, cp: ConstantPool, *, is_method: bool) -> list[Member]:
    count = r.u2()
    out = []
    for _ in range(count):
        access = r.u2()
        name = cp.utf8(r.u2())
        desc = cp.utf8(r.u2())
        attrs = _parse_attributes(r, cp)
        m = Member(access_flags=access, name=name, descriptor=desc, attributes=attrs)
        if is_method:
            for a in attrs:
                if a.name == "Code":
                    m.code = _parse_code_attribute(a.data, cp)
                    break
        out.append(m)
    return out


def parse(buf: bytes) -> ClassFile:
    """Parse a .class file from raw bytes into a ClassFile."""
    r = _Reader(buf)
    magic = r.u4()
    if magic != 0xCAFEBABE:
        raise ClassFormatError("bad magic 0x%08X (not a .class file)" % magic)
    minor = r.u2()
    major = r.u2()
    cp = _parse_constant_pool(r)
    access = r.u2()
    this_class = cp.class_name(r.u2())
    super_idx = r.u2()
    super_class = cp.class_name(super_idx) if super_idx != 0 else None
    n_ifaces = r.u2()
    interfaces = [cp.class_name(r.u2()) for _ in range(n_ifaces)]
    fields = _parse_members(r, cp, is_method=False)
    methods = _parse_members(r, cp, is_method=True)
    attrs = _parse_attributes(r, cp)

    source = None
    for a in attrs:
        if a.name == "SourceFile":
            source = cp.utf8(struct.unpack_from(">H", a.data, 0)[0])
            break

    return ClassFile(
        minor_version=minor, major_version=major, constant_pool=cp,
        access_flags=access, this_class=this_class, super_class=super_class,
        interfaces=interfaces, fields=fields, methods=methods,
        attributes=attrs, source_name=source)


def parse_file(path: str) -> ClassFile:
    with open(path, "rb") as f:
        return parse(f.read())


# ---------------------------------------------------------------------------
# Descriptor helpers (JVMS 4.3)
# ---------------------------------------------------------------------------
def parse_field_descriptor(desc: str) -> str:
    """Return a normalized type string for a field descriptor (no parsing of
    parameter lists). e.g. '[Ljava/lang/String;' -> '[Ljava/lang/String;'."""
    return desc


def parse_method_descriptor(desc: str) -> tuple[list[str], str]:
    """Split a method descriptor into (param_types, return_type), each a raw
    JVM type string like 'I', 'J', '[B', 'Ljava/lang/String;'."""
    if not desc.startswith("("):
        raise ClassFormatError("bad method descriptor: %r" % desc)
    i = 1
    params = []
    while desc[i] != ")":
        t, i = _read_one_type(desc, i)
        params.append(t)
    ret, _ = _read_one_type(desc, i + 1)
    return params, ret


def _read_one_type(desc: str, i: int) -> tuple[str, int]:
    start = i
    while desc[i] == "[":
        i += 1
    c = desc[i]
    if c == "L":
        end = desc.index(";", i)
        return desc[start:end + 1], end + 1
    if c in "BCDFIJSZV":
        return desc[start:i + 1], i + 1
    raise ClassFormatError("bad type char %r in %r at %d" % (c, desc, i))


# Sizes in JVM local/stack slots: long/double take two.
def type_slots(t: str) -> int:
    return 2 if t in ("J", "D") else 1
