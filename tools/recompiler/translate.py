"""
translate.py -- JVM bytecode -> C for a single method.

Model: the operand stack becomes a flat set of typed C locals. At every program
point the stack height and the type of each slot are fixed (JVM verification
guarantees this), so we:

  1. decode the method's instructions,
  2. run a forward abstract interpretation that records the stack-type state
     (a list of one-char tags i/l/f/d/a) entering each instruction,
  3. emit C using stack variables named  st<index>_<tag>  and locals named
     loc<slot>_<tag>.

long/double are a single stack *entry* (category 2) tagged 'l'/'d'. Control flow
becomes labels + goto. Exceptions become a setjmp frame (j_try) plus a _pc
cursor used to pick the matching handler.

External (runtime) symbols referenced by the code are accumulated on the
Codegen so the driver can emit extern declarations.
"""

from __future__ import annotations

import classfile
import mangle
import opcodes
from layout import (Program, comp_ctype, type_tag, parse_method_descriptor)


def cat(tag: str) -> int:
    return 2 if tag in ("l", "d") else 1


_BIN_INT = {"iadd": "+", "isub": "-", "imul": "*",
            "iand": "&", "ior": "|", "ixor": "^"}
_BIN_LONG = {"ladd": "+", "lsub": "-", "lmul": "*",
             "land": "&", "lor": "|", "lxor": "^"}
_BIN_FLOAT = {"fadd": "+", "fsub": "-", "fmul": "*", "fdiv": "/"}
_BIN_DOUBLE = {"dadd": "+", "dsub": "-", "dmul": "*", "ddiv": "/"}

_CONST_I = {"iconst_m1": -1, "iconst_0": 0, "iconst_1": 1, "iconst_2": 2,
            "iconst_3": 3, "iconst_4": 4, "iconst_5": 5}
_LOAD_N = {  # name -> (base_op, fixed_index)
    "iload_0": ("iload", 0), "iload_1": ("iload", 1), "iload_2": ("iload", 2), "iload_3": ("iload", 3),
    "lload_0": ("lload", 0), "lload_1": ("lload", 1), "lload_2": ("lload", 2), "lload_3": ("lload", 3),
    "fload_0": ("fload", 0), "fload_1": ("fload", 1), "fload_2": ("fload", 2), "fload_3": ("fload", 3),
    "dload_0": ("dload", 0), "dload_1": ("dload", 1), "dload_2": ("dload", 2), "dload_3": ("dload", 3),
    "aload_0": ("aload", 0), "aload_1": ("aload", 1), "aload_2": ("aload", 2), "aload_3": ("aload", 3),
}
_STORE_N = {
    "istore_0": ("istore", 0), "istore_1": ("istore", 1), "istore_2": ("istore", 2), "istore_3": ("istore", 3),
    "lstore_0": ("lstore", 0), "lstore_1": ("lstore", 1), "lstore_2": ("lstore", 2), "lstore_3": ("lstore", 3),
    "fstore_0": ("fstore", 0), "fstore_1": ("fstore", 1), "fstore_2": ("fstore", 2), "fstore_3": ("fstore", 3),
    "dstore_0": ("dstore", 0), "dstore_1": ("dstore", 1), "dstore_2": ("dstore", 2), "dstore_3": ("dstore", 3),
    "astore_0": ("astore", 0), "astore_1": ("astore", 1), "astore_2": ("astore", 2), "astore_3": ("astore", 3),
}
_LOAD_TAG = {"iload": "i", "lload": "l", "fload": "f", "dload": "d", "aload": "a"}
_STORE_TAG = {"istore": "i", "lstore": "l", "fstore": "f", "dstore": "d", "astore": "a"}
_ARR_LOAD = {  # name -> (tag, helper, deref_cast or None)
    "iaload": ("i", "j_iarr"), "laload": ("l", "j_jarr"), "faload": ("f", "j_farr"),
    "daload": ("d", "j_darr"), "aaload": ("a", "j_aref"), "baload": ("i", "j_barr"),
    "caload": ("i", "j_carr"), "saload": ("i", "j_sarr"),
}
_ARR_STORE = {
    "iastore": ("i", "j_iarr"), "lastore": ("l", "j_jarr"), "fastore": ("f", "j_farr"),
    "dastore": ("d", "j_darr"), "aastore": ("a", "j_aref"), "bastore": ("i", "j_barr"),
    "castore": ("i", "j_carr"), "sastore": ("i", "j_sarr"),
}
_CMP = {"lcmp": "j_lcmp", "fcmpl": "j_fcmpl", "fcmpg": "j_fcmpg",
        "dcmpl": "j_dcmpl", "dcmpg": "j_dcmpg"}
_IF_UNARY = {"ifeq": "==", "ifne": "!=", "iflt": "<", "ifge": ">=",
             "ifgt": ">", "ifle": "<="}
_IF_ICMP = {"if_icmpeq": "==", "if_icmpne": "!=", "if_icmplt": "<",
            "if_icmpge": ">=", "if_icmpgt": ">", "if_icmple": "<="}
_IF_ACMP = {"if_acmpeq": "==", "if_acmpne": "!="}


class TranslationError(Exception):
    pass


class Codegen:
    """Accumulates external symbol references across all translated methods."""

    def __init__(self, prog: Program):
        self.prog = prog
        self.ext_methods: set[tuple[str, str, str, bool]] = set()  # owner,name,desc,is_static
        self.ext_classes: set[str] = set()
        self.ext_statics: dict[tuple[str, str, str], str] = {}     # ->c_storage

    def note_method(self, owner, name, desc, is_static):
        if not self.prog.is_own(owner):
            self.ext_methods.add((owner, name, desc, is_static))

    def note_class(self, internal):
        if not self.prog.is_own(internal):
            self.ext_classes.add(internal)

    def note_static(self, owner, name, desc, cstore):
        if not self.prog.is_own(owner):
            self.ext_statics[(owner, name, desc)] = cstore


def translate_method(cg: Codegen, cl, ml) -> str:
    return _MethodGen(cg, cl, ml).run()


class _MethodGen:
    def __init__(self, cg: Codegen, cl, ml):
        self.cg = cg
        self.prog = cg.prog
        self.cl = cl
        self.ml = ml
        self.code: classfile.CodeAttribute = ml.code
        self.cp = cl.cf.constant_pool
        self.has_handlers = bool(self.code.exception_table)
        self.instrs = []                 # list of (pc, opcode, name, operands, length)
        self.pc_index = {}
        self.targets: set[int] = set()
        self.in_state: dict[int, list[str]] = {}
        self.used_stack: set[tuple[int, str]] = set()
        self.used_locals: set[tuple[int, str]] = set()
        self.lines: list[str] = []

    # -- variable name helpers ---------------------------------------------
    def sv(self, idx: int, tag: str) -> str:
        self.used_stack.add((idx, tag))
        return "st%d_%s" % (idx, tag)

    def lv(self, slot: int, tag: str) -> str:
        self.used_locals.add((slot, tag))
        return "loc%d_%s" % (slot, tag)

    # -- decode -------------------------------------------------------------
    def decode(self):
        code = self.code.code
        pc = 0
        for op, operands, length in opcodes.iterate(code):
            name = opcodes.NAME.get(op, "??0x%02x" % op)
            if name == "wide":
                # iterate() yields ("wide", inner_op, index[, const]). Rewrite to
                # the inner instruction carrying a 16-bit local index, so the
                # normal load/store/iinc paths handle it with no special-casing.
                # operands[2:] is (index,) for loads/stores/ret -- matching "u1" --
                # and (index, const) for iinc -- matching "u1u1".
                op = operands[1]
                name = opcodes.NAME[op]
                operands = tuple(operands[2:])
            self.instrs.append((pc, op, name, operands, length))
            self.pc_index[pc] = len(self.instrs) - 1
            pc += length

    def successors(self, pc, name, operands, length):
        nxt = pc + length
        if name in opcodes.NAME.values():
            pass
        if name == "goto":
            return [pc + operands[0]], False
        if name == "goto_w":
            return [pc + operands[0]], False
        if name in _IF_UNARY or name in _IF_ICMP or name in _IF_ACMP \
                or name in ("ifnull", "ifnonnull"):
            return [pc + operands[0], nxt], True
        if name == "tableswitch":
            _, default, low, high, offs = operands
            tgts = [pc + default] + [pc + o for o in offs]
            return tgts, False
        if name == "lookupswitch":
            _, default, pairs = operands
            tgts = [pc + default] + [pc + o for (_m, o) in pairs]
            return tgts, False
        if name in ("ireturn", "lreturn", "freturn", "dreturn", "areturn",
                    "return", "athrow"):
            return [], False
        return [nxt], True

    # -- abstract interpretation of stack types -----------------------------
    def analyze(self):
        # seed entry
        work = [(0, [])]
        for e in self.code.exception_table:
            self.targets.add(e.handler_pc)
            work.append((e.handler_pc, ["a"]))
        # Labels are emitted only at *jump* targets (and handler entries), never
        # at plain fall-through points -- otherwise the C is littered with
        # thousands of unreferenced labels.
        for (pc, op, name, operands, length) in self.instrs:
            if name in ("goto", "goto_w"):
                self.targets.add(pc + operands[0])
            elif name in _IF_UNARY or name in _IF_ICMP or name in _IF_ACMP \
                    or name in ("ifnull", "ifnonnull"):
                self.targets.add(pc + operands[0])
            elif name == "tableswitch":
                _, default, low, high, offs = operands
                self.targets.add(pc + default)
                for o in offs:
                    self.targets.add(pc + o)
            elif name == "lookupswitch":
                _, default, pairs = operands
                self.targets.add(pc + default)
                for _m, o in pairs:
                    self.targets.add(pc + o)

        seen = {}
        while work:
            pc, state = work.pop()
            if pc in seen:
                if seen[pc] != state:
                    raise TranslationError(
                        "stack mismatch at pc=%d in %s.%s%s: %r vs %r" %
                        (pc, self.ml.owner, self.ml.name, self.ml.desc,
                         seen[pc], state))
                continue
            seen[pc] = state
            self.in_state[pc] = state
            idx = self.pc_index[pc]
            _pc, op, name, operands, length = self.instrs[idx]
            out = self.transfer(name, operands, state)
            tgts, ft = self.successors(pc, name, operands, length)
            for t in tgts:
                work.append((t, out))

    def transfer(self, name, operands, state) -> list[str]:
        """Pure stack-type effect; returns the out-state tag list."""
        s = list(state)

        def pop(n=1):
            for _ in range(n):
                s.pop()

        def push(*tags):
            s.extend(tags)

        if name in _CONST_I or name == "bipush" or name == "sipush":
            push("i")
        elif name == "aconst_null":
            push("a")
        elif name in ("lconst_0", "lconst_1"):
            push("l")
        elif name in ("fconst_0", "fconst_1", "fconst_2"):
            push("f")
        elif name in ("dconst_0", "dconst_1"):
            push("d")
        elif name == "ldc" or name == "ldc_w":
            kind, _ = self.cp.ldc_value(operands[0])
            push("f" if kind == "float" else ("i" if kind == "int" else "a"))
        elif name == "ldc2_w":
            kind, _ = self.cp.ldc_value(operands[0])
            push("l" if kind == "long" else "d")
        elif name in _LOAD_N:
            base, _i = _LOAD_N[name]
            push(_LOAD_TAG[base])
        elif name in ("iload", "lload", "fload", "dload", "aload"):
            push(_LOAD_TAG[name])
        elif name in _STORE_N:
            pop()
        elif name in ("istore", "lstore", "fstore", "dstore", "astore"):
            pop()
        elif name in _ARR_LOAD:
            tag = _ARR_LOAD[name][0]; pop(2); push(tag)
        elif name in _ARR_STORE:
            pop(3)
        elif name == "pop":
            pop()
        elif name == "pop2":
            if cat(s[-1]) == 2: pop()
            else: pop(2)
        elif name == "dup":
            push(s[-1])
        elif name == "dup_x1":
            a = s[-1]; b = s[-2]; s[-2:] = [a, b, a]
        elif name == "dup_x2":
            a = s[-1]
            if cat(s[-2]) == 2: s[-2:] = [a, s[-2], a]
            else: s[-3:] = [a, s[-3], s[-2], a]
        elif name == "dup2":
            if cat(s[-1]) == 2: push(s[-1])
            else: s.extend([s[-2], s[-1]])
        elif name == "dup2_x1":
            if cat(s[-1]) == 2: s[-2:] = [s[-1], s[-2], s[-1]]
            else: s[-3:] = [s[-2], s[-1], s[-3], s[-2], s[-1]]
        elif name == "dup2_x2":
            if cat(s[-1]) == 2 and cat(s[-2]) == 2:
                s[-2:] = [s[-1], s[-2], s[-1]]
            elif cat(s[-1]) == 2:
                s[-3:] = [s[-1], s[-3], s[-2], s[-1]]
            elif cat(s[-3]) == 2:
                s[-3:] = [s[-2], s[-1], s[-3], s[-2], s[-1]]
            else:
                s[-4:] = [s[-2], s[-1], s[-4], s[-3], s[-2], s[-1]]
        elif name == "swap":
            s[-2], s[-1] = s[-1], s[-2]
        elif name in _BIN_INT or name in _BIN_LONG or name in _BIN_FLOAT or name in _BIN_DOUBLE:
            pop()  # binary: pop 2, push 1
        elif name in ("idiv", "irem", "ldiv", "lrem"):
            pop()
        elif name in ("ishl", "ishr", "iushr"):
            pop()  # value, shift(int) -> value
        elif name in ("lshl", "lshr", "lushr"):
            pop()  # long, int(shift) -> long ; pop the int
        elif name in ("ineg", "lneg", "fneg", "dneg"):
            pass
        elif name == "iinc":
            pass
        elif name in ("i2l",): pop(); push("l")
        elif name in ("i2f",): pop(); push("f")
        elif name in ("i2d",): pop(); push("d")
        elif name in ("l2i",): pop(); push("i")
        elif name in ("l2f",): pop(); push("f")
        elif name in ("l2d",): pop(); push("d")
        elif name in ("f2i",): pop(); push("i")
        elif name in ("f2l",): pop(); push("l")
        elif name in ("f2d",): pop(); push("d")
        elif name in ("d2i",): pop(); push("i")
        elif name in ("d2l",): pop(); push("l")
        elif name in ("d2f",): pop(); push("f")
        elif name in ("i2b", "i2c", "i2s"):
            pass
        elif name in _CMP:
            pop(2); push("i")  # two cat-2 (or cat-1 floats) entries -> int
        elif name in _IF_UNARY or name in ("ifnull", "ifnonnull"):
            pop()
        elif name in _IF_ICMP or name in _IF_ACMP:
            pop(2)
        elif name == "goto" or name == "goto_w":
            pass
        elif name == "tableswitch" or name == "lookupswitch":
            pop()
        elif name in ("ireturn", "freturn", "areturn", "lreturn", "dreturn"):
            pop()
        elif name == "return" or name == "nop":
            pass
        elif name == "getstatic":
            _o, _n, d = self.cp.ref(operands[0]); push(type_tag(d))
        elif name == "putstatic":
            pop()
        elif name == "getfield":
            _o, _n, d = self.cp.ref(operands[0]); pop(); push(type_tag(d))
        elif name == "putfield":
            _o, _n, d = self.cp.ref(operands[0]); pop(2)
        elif name in ("invokevirtual", "invokespecial", "invokeinterface", "invokestatic"):
            owner, mname, desc = self.cp.ref(operands[0])
            params, ret = parse_method_descriptor(desc)
            npop = len(params) + (0 if name == "invokestatic" else 1)
            pop(npop)
            if ret != "V":
                push(type_tag(ret))
        elif name == "new":
            push("a")
        elif name == "newarray" or name == "anewarray":
            pop(); push("a")
        elif name == "multianewarray":
            _idx, dims = operands; pop(dims); push("a")
        elif name == "arraylength":
            pop(); push("i")
        elif name == "athrow":
            pop()
        elif name == "checkcast":
            pass
        elif name == "instanceof":
            pop(); push("i")
        elif name in ("monitorenter", "monitorexit"):
            pop()
        else:
            raise TranslationError("transfer: unhandled opcode %s" % name)
        return s

    # -- code emission ------------------------------------------------------
    def emit(self, line=""):
        self.lines.append(line)

    def run(self) -> str:
        if self.code is None:
            return ""
        self.decode()
        self.analyze()

        body: list[str] = []
        self._save_lines = self.lines
        for (pc, op, name, operands, length) in self.instrs:
            if pc not in self.in_state:
                continue  # unreachable code (dead); skip
            self.lines = []
            if pc in self.targets:
                self.emit("L%d:;" % pc)
            if self.has_handlers:
                self.emit("_pc = %d;" % pc)
            self.gen(pc, name, operands, length)
            body.extend(self.lines)
        self.lines = self._save_lines

        return self._render(body)

    def _render(self, body) -> str:
        ml = self.ml
        out = []
        out.append(ml.c_signature() + " {")
        # declarations
        decls = []
        for (slot, tag) in sorted(self.used_locals):
            decls.append("    %s loc%d_%s = 0;" % (_ctype_for_tag(tag), slot, tag))
        for (idx, tag) in sorted(self.used_stack):
            decls.append("    %s st%d_%s = 0;" % (_ctype_for_tag(tag), idx, tag))
        if self.has_handlers:
            decls.append("    int _pc = 0;")
            decls.append("    j_eh _eh;")
        out.extend(decls)
        # bind parameters into local slots
        slot = 0
        if not ml.is_static:
            out.append("    loc0_a = this_;" if (0, "a") in self.used_locals else "    /* this unused */")
            slot = 1
        for i, p in enumerate(ml.params):
            tag = type_tag(p)
            if (slot, tag) in self.used_locals:
                out.append("    loc%d_%s = a%d;" % (slot, tag, i))
            slot += cat(tag)
        # exception landing pad
        if self.has_handlers:
            out.append("    if (j_try(&_eh)) {")
            out.append("        jref _ex = _eh.ex;")
            for e in self.code.exception_table:
                if e.catch_type == 0:
                    cond = "1"
                else:
                    cn = self.cp.class_name(e.catch_type)
                    self.cg.note_class(cn)
                    cond = "j_instanceof(_ex, &%s)" % mangle.class_descriptor(cn)
                out.append("        if (_pc >= %d && _pc < %d && %s) goto L%d;"
                           % (e.start_pc, e.end_pc, cond, e.handler_pc))
            out.append("        j_rethrow(_ex);")
            out.append("    }")
        out.extend(body)
        out.append("}")
        return "\n".join(out)

    def _clinit_guard(self, owner):
        """Emit `j_clinit_<owner>();` before an active use of an own class that
        has a <clinit> -- lazy init matching JVM semantics. Returns None if no
        guard is needed (runtime class, or no static initializer)."""
        if self.prog.is_own(owner) and self.prog.classes[owner].has_clinit:
            return "j_clinit_%s();" % mangle.cls(owner)
        return None

    # -- the big per-opcode generator --------------------------------------
    def gen(self, pc, name, operands, length):
        st = self.in_state[pc]
        h = len(st)
        S = self.sv
        emit = self.emit
        nxt = pc + length

        def ret_pop_prefix():
            return "j_pop_eh(); " if self.has_handlers else ""

        # constants
        if name in _CONST_I:
            emit("%s = %d;" % (S(h, "i"), _CONST_I[name])); return
        if name == "bipush" or name == "sipush":
            emit("%s = %d;" % (S(h, "i"), operands[0])); return
        if name == "aconst_null":
            emit("%s = (jref)0;" % S(h, "a")); return
        if name in ("lconst_0", "lconst_1"):
            emit("%s = %dLL;" % (S(h, "l"), 0 if name.endswith("0") else 1)); return
        if name in ("fconst_0", "fconst_1", "fconst_2"):
            emit("%s = %sf;" % (S(h, "f"), name[-1])); return
        if name in ("dconst_0", "dconst_1"):
            emit("%s = %s.0;" % (S(h, "d"), name[-1])); return
        if name in ("ldc", "ldc_w", "ldc2_w"):
            self._gen_ldc(operands[0], h); return

        # loads / stores
        if name in _LOAD_N:
            base, fi = _LOAD_N[name]; tag = _LOAD_TAG[base]
            emit("%s = %s;" % (S(h, tag), self.lv(fi, tag))); return
        if name in ("iload", "lload", "fload", "dload", "aload"):
            tag = _LOAD_TAG[name]
            emit("%s = %s;" % (S(h, tag), self.lv(operands[0], tag))); return
        if name in _STORE_N:
            base, fi = _STORE_N[name]; tag = _STORE_TAG[base]
            emit("%s = %s;" % (self.lv(fi, tag), S(h - 1, tag))); return
        if name in ("istore", "lstore", "fstore", "dstore", "astore"):
            tag = _STORE_TAG[name]
            emit("%s = %s;" % (self.lv(operands[0], tag), S(h - 1, tag))); return

        # arithmetic
        if name in _BIN_INT:
            emit("%s = %s %s %s;" % (S(h - 2, "i"), S(h - 2, "i"), _BIN_INT[name], S(h - 1, "i"))); return
        if name in _BIN_LONG:
            emit("%s = %s %s %s;" % (S(h - 2, "l"), S(h - 2, "l"), _BIN_LONG[name], S(h - 1, "l"))); return
        if name in _BIN_FLOAT:
            emit("%s = %s %s %s;" % (S(h - 2, "f"), S(h - 2, "f"), _BIN_FLOAT[name], S(h - 1, "f"))); return
        if name in _BIN_DOUBLE:
            emit("%s = %s %s %s;" % (S(h - 2, "d"), S(h - 2, "d"), _BIN_DOUBLE[name], S(h - 1, "d"))); return
        if name == "idiv":
            emit("%s = j_idiv(%s, %s);" % (S(h - 2, "i"), S(h - 2, "i"), S(h - 1, "i"))); return
        if name == "irem":
            emit("%s = j_irem(%s, %s);" % (S(h - 2, "i"), S(h - 2, "i"), S(h - 1, "i"))); return
        if name == "ldiv":
            emit("%s = j_ldiv(%s, %s);" % (S(h - 2, "l"), S(h - 2, "l"), S(h - 1, "l"))); return
        if name == "lrem":
            emit("%s = j_lrem(%s, %s);" % (S(h - 2, "l"), S(h - 2, "l"), S(h - 1, "l"))); return
        if name in ("ishl", "ishr"):
            opc = "<<" if name == "ishl" else ">>"
            emit("%s = %s %s (%s & 31);" % (S(h - 2, "i"), S(h - 2, "i"), opc, S(h - 1, "i"))); return
        if name == "iushr":
            emit("%s = (jint)((uint32_t)%s >> (%s & 31));" % (S(h - 2, "i"), S(h - 2, "i"), S(h - 1, "i"))); return
        if name in ("lshl", "lshr"):
            opc = "<<" if name == "lshl" else ">>"
            emit("%s = %s %s (%s & 63);" % (S(h - 2, "l"), S(h - 2, "l"), opc, S(h - 1, "i"))); return
        if name == "lushr":
            emit("%s = (jlong)((uint64_t)%s >> (%s & 63));" % (S(h - 2, "l"), S(h - 2, "l"), S(h - 1, "i"))); return
        if name == "ineg":
            emit("%s = -%s;" % (S(h - 1, "i"), S(h - 1, "i"))); return
        if name == "lneg":
            emit("%s = -%s;" % (S(h - 1, "l"), S(h - 1, "l"))); return
        if name == "fneg":
            emit("%s = -%s;" % (S(h - 1, "f"), S(h - 1, "f"))); return
        if name == "dneg":
            emit("%s = -%s;" % (S(h - 1, "d"), S(h - 1, "d"))); return
        if name == "iinc":
            idx, c = operands
            emit("%s += %d;" % (self.lv(idx, "i"), c)); return

        # conversions
        conv = {"i2l": ("l", "(jlong)"), "i2f": ("f", "(jfloat)"), "i2d": ("d", "(jdouble)"),
                "l2i": ("i", "(jint)"), "l2f": ("f", "(jfloat)"), "l2d": ("d", "(jdouble)"),
                "f2d": ("d", "(jdouble)"), "d2f": ("f", "(jfloat)")}
        if name in conv:
            dtag, cast = conv[name]
            stag = {"i": "i", "l": "l", "f": "f", "d": "d"}[name[0]]
            emit("%s = %s%s;" % (S(h - 1, dtag), cast, S(h - 1, stag))); return
        if name in ("f2i", "d2i"):
            stag = "f" if name == "f2i" else "d"
            emit("%s = (jint)%s;" % (S(h - 1, "i"), S(h - 1, stag))); return
        if name in ("f2l", "d2l"):
            stag = "f" if name == "f2l" else "d"
            emit("%s = (jlong)%s;" % (S(h - 1, "l"), S(h - 1, stag))); return
        if name == "i2b":
            emit("%s = (jint)(jbyte)%s;" % (S(h - 1, "i"), S(h - 1, "i"))); return
        if name == "i2c":
            emit("%s = (jint)(jchar)%s;" % (S(h - 1, "i"), S(h - 1, "i"))); return
        if name == "i2s":
            emit("%s = (jint)(jshort)%s;" % (S(h - 1, "i"), S(h - 1, "i"))); return

        # comparisons producing int
        if name in _CMP:
            t = "l" if name == "lcmp" else ("f" if name.startswith("f") else "d")
            emit("%s = %s(%s, %s);" % (S(h - 2, "i"), _CMP[name], S(h - 2, t), S(h - 1, t))); return

        # branches
        if name in _IF_UNARY:
            emit("if (%s %s 0) goto L%d;" % (S(h - 1, "i"), _IF_UNARY[name], pc + operands[0])); return
        if name == "ifnull":
            emit("if (%s == (jref)0) goto L%d;" % (S(h - 1, "a"), pc + operands[0])); return
        if name == "ifnonnull":
            emit("if (%s != (jref)0) goto L%d;" % (S(h - 1, "a"), pc + operands[0])); return
        if name in _IF_ICMP:
            emit("if (%s %s %s) goto L%d;" % (S(h - 2, "i"), _IF_ICMP[name], S(h - 1, "i"), pc + operands[0])); return
        if name in _IF_ACMP:
            emit("if (%s %s %s) goto L%d;" % (S(h - 2, "a"), _IF_ACMP[name], S(h - 1, "a"), pc + operands[0])); return
        if name in ("goto", "goto_w"):
            emit("goto L%d;" % (pc + operands[0])); return

        if name == "tableswitch":
            _, default, low, high, offs = operands
            emit("switch (%s) {" % S(h - 1, "i"))
            for k, off in enumerate(offs):
                emit("    case %d: goto L%d;" % (low + k, pc + off))
            emit("    default: goto L%d;" % (pc + default))
            emit("}"); return
        if name == "lookupswitch":
            _, default, pairs = operands
            emit("switch (%s) {" % S(h - 1, "i"))
            for m, off in pairs:
                emit("    case %d: goto L%d;" % (m, pc + off))
            emit("    default: goto L%d;" % (pc + default))
            emit("}"); return

        # returns
        if name == "return":
            emit("%sreturn;" % ret_pop_prefix()); return
        if name in ("ireturn", "lreturn", "freturn", "dreturn", "areturn"):
            tag = {"ireturn": "i", "lreturn": "l", "freturn": "f",
                   "dreturn": "d", "areturn": "a"}[name]
            emit("%sreturn %s;" % (ret_pop_prefix(), S(h - 1, tag))); return

        # fields
        if name == "getstatic":
            owner, fname, desc = self.cp.ref(operands[0])
            g, _own = self.prog.static_field_member(owner, fname, desc)
            self.cg.note_static(owner, fname, desc, _storage_ctype(desc))
            guard = self._clinit_guard(owner)
            if guard: emit(guard)
            emit("%s = %s;" % (S(h, type_tag(desc)), g)); return
        if name == "putstatic":
            owner, fname, desc = self.cp.ref(operands[0])
            g, _own = self.prog.static_field_member(owner, fname, desc)
            self.cg.note_static(owner, fname, desc, _storage_ctype(desc))
            guard = self._clinit_guard(owner)
            if guard: emit(guard)
            emit("%s = %s;" % (g, S(h - 1, type_tag(desc)))); return
        if name == "getfield":
            owner, fname, desc = self.cp.ref(operands[0])
            fl = self.prog.resolve_instance_field(owner, fname, desc)
            emit("%s = %s(%s, %s);" % (S(h - 1, type_tag(desc)), fl.access_macro,
                                       S(h - 1, "a"), fl.off_expr)); return
        if name == "putfield":
            owner, fname, desc = self.cp.ref(operands[0])
            fl = self.prog.resolve_instance_field(owner, fname, desc)
            emit("%s(%s, %s) = %s;" % (fl.access_macro, S(h - 2, "a"), fl.off_expr,
                                       S(h - 1, type_tag(desc)))); return

        # method invocation
        if name in ("invokevirtual", "invokespecial", "invokeinterface", "invokestatic"):
            self._gen_invoke(name, operands, h); return

        # object / array creation
        if name == "new":
            cn = self.cp.class_name(operands[0]); self.cg.note_class(cn)
            guard = self._clinit_guard(cn)
            if guard: emit(guard)
            emit("%s = j_new(&%s);" % (S(h, "a"), mangle.class_descriptor(cn))); return
        if name == "newarray":
            atype = operands[0]
            emit("%s = j_newarray(%d, %s);" % (S(h - 1, "a"), atype, S(h - 1, "i"))); return
        if name == "anewarray":
            cn = self.cp.class_name(operands[0]); self.cg.note_class(cn)
            emit("%s = j_anewarray(&%s, %s);" % (S(h - 1, "a"),
                 mangle.class_descriptor(cn), S(h - 1, "i"))); return
        if name == "multianewarray":
            idx, dims = operands
            cn = self.cp.class_name(idx); self.cg.note_class(cn)
            args = ", ".join(S(h - dims + k, "i") for k in range(dims))
            emit("{ jint _d[%d] = { %s };" % (dims, args))
            emit("  %s = j_multianewarray(&%s, %d, _d); }" %
                 (S(h - dims, "a"), mangle.class_descriptor(cn), dims)); return
        if name == "arraylength":
            emit("%s = j_arraylength(%s);" % (S(h - 1, "i"), S(h - 1, "a"))); return

        # array element access
        if name in _ARR_LOAD:
            tag, helper = _ARR_LOAD[name]
            emit("%s = *%s(%s, %s);" % (S(h - 2, tag), helper, S(h - 2, "a"), S(h - 1, "i"))); return
        if name in _ARR_STORE:
            tag, helper = _ARR_STORE[name]
            emit("*%s(%s, %s) = %s;" % (helper, S(h - 3, "a"), S(h - 2, "i"), S(h - 1, tag))); return

        # type checks
        if name == "checkcast":
            cn = self.cp.class_name(operands[0]); self.cg.note_class(cn)
            emit("%s = j_checkcast(%s, &%s);" % (S(h - 1, "a"), S(h - 1, "a"),
                 mangle.class_descriptor(cn))); return
        if name == "instanceof":
            cn = self.cp.class_name(operands[0]); self.cg.note_class(cn)
            emit("%s = j_instanceof(%s, &%s);" % (S(h - 1, "i"), S(h - 1, "a"),
                 mangle.class_descriptor(cn))); return

        # exceptions / monitors
        if name == "athrow":
            emit("j_throw(%s);" % S(h - 1, "a")); return
        if name in ("monitorenter", "monitorexit"):
            emit("(void)%s; /* %s: single-threaded, no-op */" % (S(h - 1, "a"), name)); return

        # stack manipulation
        if name in ("pop", "pop2", "dup", "dup_x1", "dup_x2", "dup2",
                    "dup2_x1", "dup2_x2", "swap"):
            self._gen_stackop(name, st, h); return

        if name == "nop":
            emit("/* nop */"); return

        raise TranslationError("gen: unhandled opcode %s at pc=%d in %s.%s%s"
                               % (name, pc, self.ml.owner, self.ml.name, self.ml.desc))

    def _gen_ldc(self, cpidx, h):
        kind, val = self.cp.ldc_value(cpidx)
        S = self.sv
        if kind == "int":
            self.emit("%s = %d;" % (S(h, "i"), val))
        elif kind == "float":
            self.emit("%s = %sf;" % (S(h, "f"), repr(float(val))))
        elif kind == "long":
            self.emit("%s = %dLL;" % (S(h, "l"), val))
        elif kind == "double":
            self.emit("%s = %s;" % (S(h, "d"), repr(float(val))))
        elif kind == "String":
            self.emit('%s = j_strlit(%s);' % (S(h, "a"), _cstr(val)))
        elif kind == "Class":
            self.cg.note_class(val)
            self.emit("%s = j_class_object(&%s);" % (S(h, "a"), mangle.class_descriptor(val)))
        else:
            raise TranslationError("ldc kind %s" % kind)

    def _gen_invoke(self, name, operands, h):
        owner, mname, desc = self.cp.ref(operands[0])
        params, ret = parse_method_descriptor(desc)
        is_static = (name == "invokestatic")
        npop = len(params) + (0 if is_static else 1)
        base = h - npop
        arg_exprs = []
        if not is_static:
            obj_var = self.sv(base, "a")
        # parameter entries occupy base(+1 if instance) .. h-1
        pbase = base + (0 if is_static else 1)
        for i, p in enumerate(params):
            arg_exprs.append(self.sv(pbase + i, type_tag(p)))

        self.cg.note_method(owner, mname, desc, is_static)
        ret_c = comp_ctype(ret)

        if is_static:
            guard = self._clinit_guard(owner)
            if guard:
                self.emit(guard)

        if name in ("invokestatic", "invokespecial"):
            fn = mangle.method(owner, mname, desc)
            call_args = ([] if is_static else [obj_var]) + arg_exprs
            call = "%s(%s)" % (fn, ", ".join(call_args))
        else:
            # virtual / interface: runtime method search on the dynamic class
            cast = self._fnptr_cast(ret, params, instance=True)
            fp = 'j_vfind(((jref)%s)->cls, %s, %s)' % (obj_var, _cstr(mname), _cstr(desc))
            call_args = [obj_var] + arg_exprs
            call = "((%s)%s)(%s)" % (cast, fp, ", ".join(call_args))

        if ret == "V":
            self.emit("%s;" % call)
        else:
            self.emit("%s = %s;" % (self.sv(base, type_tag(ret)), call))

    def _fnptr_cast(self, ret, params, instance):
        rc = comp_ctype(ret)
        ps = (["jref"] if instance else []) + [comp_ctype(p) for p in params]
        if not ps:
            ps = ["void"]
        return "%s (*)(%s)" % (rc, ", ".join(ps))

    def _gen_stackop(self, name, st, h):
        """dup/pop/swap family via a temp snapshot (always correct, incl. equal tags)."""
        S = self.sv
        # Determine how many top entries are involved and the output reference list.
        def ref(idx):  # symbolic value at stack index idx, with its tag
            return (idx, st[idx])
        if name == "pop":
            return  # value just discarded; out-state already shorter
        if name == "pop2":
            return
        if name == "swap":
            a = ref(h - 1); b = ref(h - 2)
            self._emit_perm([b, a], h - 2, reversed_in=[a, b]); return
        if name == "dup":
            a = ref(h - 1)
            self._emit_perm([a, a], h - 1, reversed_in=[a]); return
        if name == "dup_x1":
            a = ref(h - 1); b = ref(h - 2)
            self._emit_perm([a, b, a], h - 2, reversed_in=[b, a]); return
        if name == "dup_x2":
            a = ref(h - 1)
            if cat(st[h - 2]) == 2:
                b = ref(h - 2); self._emit_perm([a, b, a], h - 2, reversed_in=[b, a])
            else:
                b = ref(h - 2); c = ref(h - 3)
                self._emit_perm([a, c, b, a], h - 3, reversed_in=[c, b, a])
            return
        if name == "dup2":
            if cat(st[h - 1]) == 2:
                a = ref(h - 1); self._emit_perm([a, a], h - 1, reversed_in=[a])
            else:
                a = ref(h - 2); b = ref(h - 1)
                self._emit_perm([a, b, a, b], h - 2, reversed_in=[a, b])
            return
        if name == "dup2_x1":
            if cat(st[h - 1]) == 2:
                a = ref(h - 1); b = ref(h - 2)
                self._emit_perm([a, b, a], h - 2, reversed_in=[b, a])
            else:
                a = ref(h - 2); b = ref(h - 1); c = ref(h - 3)
                self._emit_perm([a, b, c, a, b], h - 3, reversed_in=[c, a, b])
            return
        if name == "dup2_x2":
            c1 = cat(st[h - 1]); c2 = cat(st[h - 2])
            if c1 == 2 and c2 == 2:
                a = ref(h - 1); b = ref(h - 2)
                self._emit_perm([a, b, a], h - 2, reversed_in=[b, a])
            elif c1 == 2:
                a = ref(h - 1); b = ref(h - 2); c = ref(h - 3)
                self._emit_perm([a, c, b, a], h - 3, reversed_in=[c, b, a])
            elif cat(st[h - 3]) == 2:
                a = ref(h - 2); b = ref(h - 1); c = ref(h - 3)
                self._emit_perm([a, b, c, a, b], h - 3, reversed_in=[c, a, b])
            else:
                a = ref(h - 2); b = ref(h - 1); c = ref(h - 4); d = ref(h - 3)
                self._emit_perm([a, b, c, d, a, b], h - 4, reversed_in=[c, d, a, b])
            return

    def _emit_perm(self, out_refs, base, reversed_in):
        """Snapshot involved source values into temps, then write outputs in order.
        out_refs / reversed_in are lists of (orig_index, tag). Outputs are written
        starting at stack index `base`."""
        # snapshot every distinct source (orig_index, tag) into a temp
        srcs = []
        seen = set()
        for (idx, tag) in out_refs:
            if (idx, tag) not in seen:
                seen.add((idx, tag)); srcs.append((idx, tag))
        self.emit("{")
        tmap = {}
        for k, (idx, tag) in enumerate(srcs):
            tmap[(idx, tag)] = "_t%d" % k
            self.emit("    %s _t%d = %s;" % (_ctype_for_tag(tag), k, self.sv(idx, tag)))
        for j, (idx, tag) in enumerate(out_refs):
            self.emit("    %s = %s;" % (self.sv(base + j, tag), tmap[(idx, tag)]))
        self.emit("}")


# ---- small helpers ---------------------------------------------------------
def _ctype_for_tag(tag: str) -> str:
    return {"i": "jint", "l": "jlong", "f": "jfloat", "d": "jdouble", "a": "jref"}[tag]


def _storage_ctype(desc: str) -> str:
    from layout import storage
    return storage(desc)[0]


def _cstr(s: str) -> str:
    """Render a Python str as a C UTF-8 string literal."""
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif 0x20 <= o < 0x7F:
            out.append(ch)
        else:
            # Octal, not \xNN: C hex escapes are greedy, so "\x99" + 'a' would be
            # read as one (out-of-range) escape \x99a. Octal is capped at 3 digits.
            for b in ch.encode("utf-8"):
                out.append("\\%03o" % b)
    out.append('"')
    return "".join(out)
