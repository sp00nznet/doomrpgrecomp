"""
mangle.py -- deterministic C-identifier mangling for JVM symbols.

The recompiler and the hand-written runtime have to agree, byte for byte, on
the C name of every class struct, method function, field member, and static.
These functions are that contract. They are pure functions of the JVM symbol
(owner / name / descriptor), so a runtime author can predict the exact C name a
generated call site will use.

Scheme (chosen to be injective and human-readable, not just unique):
  class   java/lang/String           -> cls "java_lang_String"
  method  k.a(II)V                   -> "m_k__a__II__V"
  field   d.a:[B  (declared in d)    -> "f_d__a__aB"
  static  DoomRPG.a:Lk;              -> "S_DoomRPG__a__Lk"

Descriptor mangling: '(' dropped, ')' -> "__", '[' -> 'a', 'Lpkg/Cls;' ->
"L" + cls(pkg_Cls), primitives (BCDFIJSZV) kept verbatim. The result is a
reversible-enough, collision-free encoding because JVM descriptors are an
unambiguous grammar.
"""

from __future__ import annotations

import re

_BAD = re.compile(r"[^A-Za-z0-9_]")


def cls(internal_name: str) -> str:
    """'java/lang/String' -> 'java_lang_String'; '[B' stays array-ish via _arr."""
    n = internal_name.replace("/", "_").replace("$", "_S_")
    return _BAD.sub(lambda m: "_u%02x" % ord(m.group(0)), n)


def _ident(name: str) -> str:
    """Method/field simple name -> C-safe token. <init>/<clinit> handled."""
    if name == "<init>":
        return "_init_"
    if name == "<clinit>":
        return "_clinit_"
    return _BAD.sub(lambda m: "_u%02x" % ord(m.group(0)), name)


def _desc_tok(desc: str, i: int) -> tuple[str, int]:
    """Encode one type at desc[i], return (token, next_index)."""
    out = []
    while desc[i] == "[":
        out.append("a")
        i += 1
    c = desc[i]
    if c == "L":
        end = desc.index(";", i)
        out.append("L" + cls(desc[i + 1:end]))
        return "".join(out), end + 1
    # primitive (incl. V for return)
    out.append(c)
    return "".join(out), i + 1


def field_desc(desc: str) -> str:
    tok, _ = _desc_tok(desc, 0)
    return tok


def method_desc(desc: str) -> str:
    """'(II)V' -> 'II__V' ; '()Ljava/lang/String;' -> '__Ljava_lang_String'."""
    assert desc[0] == "(", desc
    i = 1
    params = []
    while desc[i] != ")":
        t, i = _desc_tok(desc, i)
        params.append(t)
    ret, _ = _desc_tok(desc, i + 1)
    return "".join(params) + "__" + ret


def method(owner: str, name: str, desc: str) -> str:
    return "m_%s__%s__%s" % (cls(owner), _ident(name), method_desc(desc))


def field_member(declaring_owner: str, name: str, desc: str) -> str:
    return "f_%s__%s__%s" % (cls(declaring_owner), _ident(name), field_desc(desc))


def static_field(owner: str, name: str, desc: str) -> str:
    return "S_%s__%s__%s" % (cls(owner), _ident(name), field_desc(desc))


def struct_name(internal_name: str) -> str:
    return "C_" + cls(internal_name)


def class_descriptor(internal_name: str) -> str:
    """The C name of the jclass metadata object for a class."""
    return "CLASS_" + cls(internal_name)
