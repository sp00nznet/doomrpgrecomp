"""
opcodes.py -- the JVM instruction set (JVMS Ch. 6).

Provides:
  * NAME[opcode]        -> mnemonic string
  * OPERANDS[opcode]    -> a format token describing fixed operands
  * iterate(code)       -> yields (opcode, operands, length) over a bytecode
                           array, correctly handling `wide`, `tableswitch`,
                           and `lookupswitch` (the three variable-length cases).

Operand format tokens (each describes the bytes that follow the opcode byte):
  ""        no operands
  "u1"      one unsigned byte           (e.g. bipush, ldc, iload)
  "s1"      one signed byte             (e.g. bipush value)
  "u2"      one unsigned short          (e.g. ldc_w, getfield, invokevirtual)
  "s2"      one signed short            (branch offsets, sipush)
  "s4"      one signed int              (goto_w, jsr_w)
  "u1u1"    iinc index, const           -> (index, signed const)
  "u2u1u1"  invokeinterface             -> (index, count, 0)
  "u1u1u1"  multianewarray? (no)        -- multianewarray is "u2u1"
  "u2u1"    multianewarray              -> (index, dimensions)
  "newarray" atype byte                 -> (atype,)
  "wide"    handled specially in iterate
  "tableswitch" / "lookupswitch"        handled specially in iterate

The iterate() output normalizes operands into a tuple of decoded ints (and,
for switches, a dict). The translator consumes this directly.
"""

from __future__ import annotations

import struct


# opcode -> (mnemonic, operand_format)
_TABLE: dict[int, tuple[str, str]] = {
    0x00: ("nop", ""),
    0x01: ("aconst_null", ""),
    0x02: ("iconst_m1", ""),
    0x03: ("iconst_0", ""),
    0x04: ("iconst_1", ""),
    0x05: ("iconst_2", ""),
    0x06: ("iconst_3", ""),
    0x07: ("iconst_4", ""),
    0x08: ("iconst_5", ""),
    0x09: ("lconst_0", ""),
    0x0A: ("lconst_1", ""),
    0x0B: ("fconst_0", ""),
    0x0C: ("fconst_1", ""),
    0x0D: ("fconst_2", ""),
    0x0E: ("dconst_0", ""),
    0x0F: ("dconst_1", ""),
    0x10: ("bipush", "s1"),
    0x11: ("sipush", "s2"),
    0x12: ("ldc", "u1"),
    0x13: ("ldc_w", "u2"),
    0x14: ("ldc2_w", "u2"),
    0x15: ("iload", "u1"),
    0x16: ("lload", "u1"),
    0x17: ("fload", "u1"),
    0x18: ("dload", "u1"),
    0x19: ("aload", "u1"),
    0x1A: ("iload_0", ""),
    0x1B: ("iload_1", ""),
    0x1C: ("iload_2", ""),
    0x1D: ("iload_3", ""),
    0x1E: ("lload_0", ""),
    0x1F: ("lload_1", ""),
    0x20: ("lload_2", ""),
    0x21: ("lload_3", ""),
    0x22: ("fload_0", ""),
    0x23: ("fload_1", ""),
    0x24: ("fload_2", ""),
    0x25: ("fload_3", ""),
    0x26: ("dload_0", ""),
    0x27: ("dload_1", ""),
    0x28: ("dload_2", ""),
    0x29: ("dload_3", ""),
    0x2A: ("aload_0", ""),
    0x2B: ("aload_1", ""),
    0x2C: ("aload_2", ""),
    0x2D: ("aload_3", ""),
    0x2E: ("iaload", ""),
    0x2F: ("laload", ""),
    0x30: ("faload", ""),
    0x31: ("daload", ""),
    0x32: ("aaload", ""),
    0x33: ("baload", ""),
    0x34: ("caload", ""),
    0x35: ("saload", ""),
    0x36: ("istore", "u1"),
    0x37: ("lstore", "u1"),
    0x38: ("fstore", "u1"),
    0x39: ("dstore", "u1"),
    0x3A: ("astore", "u1"),
    0x3B: ("istore_0", ""),
    0x3C: ("istore_1", ""),
    0x3D: ("istore_2", ""),
    0x3E: ("istore_3", ""),
    0x3F: ("lstore_0", ""),
    0x40: ("lstore_1", ""),
    0x41: ("lstore_2", ""),
    0x42: ("lstore_3", ""),
    0x43: ("fstore_0", ""),
    0x44: ("fstore_1", ""),
    0x45: ("fstore_2", ""),
    0x46: ("fstore_3", ""),
    0x47: ("dstore_0", ""),
    0x48: ("dstore_1", ""),
    0x49: ("dstore_2", ""),
    0x4A: ("dstore_3", ""),
    0x4B: ("astore_0", ""),
    0x4C: ("astore_1", ""),
    0x4D: ("astore_2", ""),
    0x4E: ("astore_3", ""),
    0x4F: ("iastore", ""),
    0x50: ("lastore", ""),
    0x51: ("fastore", ""),
    0x52: ("dastore", ""),
    0x53: ("aastore", ""),
    0x54: ("bastore", ""),
    0x55: ("castore", ""),
    0x56: ("sastore", ""),
    0x57: ("pop", ""),
    0x58: ("pop2", ""),
    0x59: ("dup", ""),
    0x5A: ("dup_x1", ""),
    0x5B: ("dup_x2", ""),
    0x5C: ("dup2", ""),
    0x5D: ("dup2_x1", ""),
    0x5E: ("dup2_x2", ""),
    0x5F: ("swap", ""),
    0x60: ("iadd", ""),
    0x61: ("ladd", ""),
    0x62: ("fadd", ""),
    0x63: ("dadd", ""),
    0x64: ("isub", ""),
    0x65: ("lsub", ""),
    0x66: ("fsub", ""),
    0x67: ("dsub", ""),
    0x68: ("imul", ""),
    0x69: ("lmul", ""),
    0x6A: ("fmul", ""),
    0x6B: ("dmul", ""),
    0x6C: ("idiv", ""),
    0x6D: ("ldiv", ""),
    0x6E: ("fdiv", ""),
    0x6F: ("ddiv", ""),
    0x70: ("irem", ""),
    0x71: ("lrem", ""),
    0x72: ("frem", ""),
    0x73: ("drem", ""),
    0x74: ("ineg", ""),
    0x75: ("lneg", ""),
    0x76: ("fneg", ""),
    0x77: ("dneg", ""),
    0x78: ("ishl", ""),
    0x79: ("lshl", ""),
    0x7A: ("ishr", ""),
    0x7B: ("lshr", ""),
    0x7C: ("iushr", ""),
    0x7D: ("lushr", ""),
    0x7E: ("iand", ""),
    0x7F: ("land", ""),
    0x80: ("ior", ""),
    0x81: ("lor", ""),
    0x82: ("ixor", ""),
    0x83: ("lxor", ""),
    0x84: ("iinc", "u1u1"),
    0x85: ("i2l", ""),
    0x86: ("i2f", ""),
    0x87: ("i2d", ""),
    0x88: ("l2i", ""),
    0x89: ("l2f", ""),
    0x8A: ("l2d", ""),
    0x8B: ("f2i", ""),
    0x8C: ("f2l", ""),
    0x8D: ("f2d", ""),
    0x8E: ("d2i", ""),
    0x8F: ("d2l", ""),
    0x90: ("d2f", ""),
    0x91: ("i2b", ""),
    0x92: ("i2c", ""),
    0x93: ("i2s", ""),
    0x94: ("lcmp", ""),
    0x95: ("fcmpl", ""),
    0x96: ("fcmpg", ""),
    0x97: ("dcmpl", ""),
    0x98: ("dcmpg", ""),
    0x99: ("ifeq", "s2"),
    0x9A: ("ifne", "s2"),
    0x9B: ("iflt", "s2"),
    0x9C: ("ifge", "s2"),
    0x9D: ("ifgt", "s2"),
    0x9E: ("ifle", "s2"),
    0x9F: ("if_icmpeq", "s2"),
    0xA0: ("if_icmpne", "s2"),
    0xA1: ("if_icmplt", "s2"),
    0xA2: ("if_icmpge", "s2"),
    0xA3: ("if_icmpgt", "s2"),
    0xA4: ("if_icmple", "s2"),
    0xA5: ("if_acmpeq", "s2"),
    0xA6: ("if_acmpne", "s2"),
    0xA7: ("goto", "s2"),
    0xA8: ("jsr", "s2"),
    0xA9: ("ret", "u1"),
    0xAA: ("tableswitch", "tableswitch"),
    0xAB: ("lookupswitch", "lookupswitch"),
    0xAC: ("ireturn", ""),
    0xAD: ("lreturn", ""),
    0xAE: ("freturn", ""),
    0xAF: ("dreturn", ""),
    0xB0: ("areturn", ""),
    0xB1: ("return", ""),
    0xB2: ("getstatic", "u2"),
    0xB3: ("putstatic", "u2"),
    0xB4: ("getfield", "u2"),
    0xB5: ("putfield", "u2"),
    0xB6: ("invokevirtual", "u2"),
    0xB7: ("invokespecial", "u2"),
    0xB8: ("invokestatic", "u2"),
    0xB9: ("invokeinterface", "u2u1u1"),
    0xBA: ("invokedynamic", "u2u1u1"),
    0xBB: ("new", "u2"),
    0xBC: ("newarray", "newarray"),
    0xBD: ("anewarray", "u2"),
    0xBE: ("arraylength", ""),
    0xBF: ("athrow", ""),
    0xC0: ("checkcast", "u2"),
    0xC1: ("instanceof", "u2"),
    0xC2: ("monitorenter", ""),
    0xC3: ("monitorexit", ""),
    0xC4: ("wide", "wide"),
    0xC5: ("multianewarray", "u2u1"),
    0xC6: ("ifnull", "s2"),
    0xC7: ("ifnonnull", "s2"),
    0xC8: ("goto_w", "s4"),
    0xC9: ("jsr_w", "s4"),
    0xCA: ("breakpoint", ""),
    0xFE: ("impdep1", ""),
    0xFF: ("impdep2", ""),
}

NAME: dict[int, str] = {op: t[0] for op, t in _TABLE.items()}
OPERANDS: dict[int, str] = {op: t[1] for op, t in _TABLE.items()}
OPCODE: dict[str, int] = {t[0]: op for op, t in _TABLE.items()}

# Opcodes that branch; operand is a relative offset from the instruction's pc.
BRANCH_S2 = {OPCODE[m] for m in (
    "ifeq", "ifne", "iflt", "ifge", "ifgt", "ifle",
    "if_icmpeq", "if_icmpne", "if_icmplt", "if_icmpge", "if_icmpgt", "if_icmple",
    "if_acmpeq", "if_acmpne", "goto", "jsr", "ifnull", "ifnonnull")}
BRANCH_S4 = {OPCODE["goto_w"], OPCODE["jsr_w"]}

RETURN_OPS = {OPCODE[m] for m in
              ("ireturn", "lreturn", "freturn", "dreturn", "areturn", "return")}


def iterate(code: bytes):
    """Yield (opcode, operands, length) for each instruction in `code`.

    `operands` is a tuple of decoded values; its shape depends on the opcode:
      * fixed-format ops -> tuple of the decoded ints (signed where appropriate)
      * wide  -> ("wide", inner_opcode, index[, const])
      * tableswitch  -> ("tableswitch", default_off, low, high, [offsets...])
      * lookupswitch -> ("lookupswitch", default_off, [(match, off), ...])

    `length` includes the opcode byte and all operand/padding bytes, so
    pc + length is the next instruction's pc.
    """
    pc = 0
    n = len(code)
    while pc < n:
        op = code[pc]
        fmt = OPERANDS.get(op)
        if fmt is None:
            # Unknown opcode: yield it with no operands, advance one byte so the
            # caller (info command) can report it rather than crash.
            yield op, (), 1
            pc += 1
            continue

        if fmt == "wide":
            inner = code[pc + 1]
            index = struct.unpack_from(">H", code, pc + 2)[0]
            if NAME.get(inner) == "iinc":
                const = struct.unpack_from(">h", code, pc + 4)[0]
                yield op, ("wide", inner, index, const), 6
                pc += 6
            else:
                yield op, ("wide", inner, index), 4
                pc += 4
            continue

        if fmt == "tableswitch":
            base = pc + 1
            pad = (4 - (base % 4)) % 4
            p = base + pad
            default = struct.unpack_from(">i", code, p)[0]
            low = struct.unpack_from(">i", code, p + 4)[0]
            high = struct.unpack_from(">i", code, p + 8)[0]
            p += 12
            offsets = []
            for _ in range(high - low + 1):
                offsets.append(struct.unpack_from(">i", code, p)[0])
                p += 4
            yield op, ("tableswitch", default, low, high, offsets), p - pc
            pc = p
            continue

        if fmt == "lookupswitch":
            base = pc + 1
            pad = (4 - (base % 4)) % 4
            p = base + pad
            default = struct.unpack_from(">i", code, p)[0]
            npairs = struct.unpack_from(">i", code, p + 4)[0]
            p += 8
            pairs = []
            for _ in range(npairs):
                match = struct.unpack_from(">i", code, p)[0]
                off = struct.unpack_from(">i", code, p + 4)[0]
                pairs.append((match, off))
                p += 8
            yield op, ("lookupswitch", default, pairs), p - pc
            pc = p
            continue

        # Fixed-format operands
        ops, length = _decode_fixed(fmt, code, pc)
        yield op, ops, length
        pc += length


def _decode_fixed(fmt: str, code: bytes, pc: int) -> tuple[tuple, int]:
    if fmt == "":
        return (), 1
    if fmt == "u1":
        return (code[pc + 1],), 2
    if fmt == "s1":
        return (struct.unpack_from(">b", code, pc + 1)[0],), 2
    if fmt == "u2":
        return (struct.unpack_from(">H", code, pc + 1)[0],), 3
    if fmt == "s2":
        return (struct.unpack_from(">h", code, pc + 1)[0],), 3
    if fmt == "s4":
        return (struct.unpack_from(">i", code, pc + 1)[0],), 5
    if fmt == "u1u1":  # iinc: index (u1), const (s1)
        return (code[pc + 1], struct.unpack_from(">b", code, pc + 2)[0]), 3
    if fmt == "u2u1u1":  # invokeinterface/invokedynamic: index, count, 0
        idx = struct.unpack_from(">H", code, pc + 1)[0]
        return (idx, code[pc + 3], code[pc + 4]), 5
    if fmt == "u2u1":  # multianewarray: index, dimensions
        idx = struct.unpack_from(">H", code, pc + 1)[0]
        return (idx, code[pc + 3]), 4
    if fmt == "newarray":
        return (code[pc + 1],), 2
    raise ValueError("unhandled operand format %r" % fmt)


# newarray atype codes (JVMS Table 6.5.newarray-A)
NEWARRAY_TYPE = {
    4: "Z", 5: "C", 6: "F", 7: "D",
    8: "B", 9: "S", 10: "I", 11: "J",
}
