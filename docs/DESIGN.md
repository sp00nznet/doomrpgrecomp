# doomrpgrecomp — design notes

Running design log. Newest decisions at the bottom of each section.

## The target

- **DoomRPG.jar** — _Doom RPG_ (id Software / Fountainhead, pub. EA), 2005.
- MIDlet manifest: `MIDP-2.0`, `CLDC-1.0`, MIDlet class `DoomRPG`, version 1.8.94.
- Class file version **45.3** (JDK 1.1-era classic format — no StackMapTable,
  no invokedynamic, no generics metadata to worry about).
- **CLDC-1.0 ⇒ no floating point** in the VM contract. The game is integer /
  fixed-point throughout (ships a `sintable.bin`). The translator still handles
  `f*`/`d*` opcodes for completeness, but we don't expect to hit them in game code.

### Inventory (from `jrecomp info`)
- 26 classes, **383 methods** (every one has a `Code` attribute — no natives,
  no abstracts in the game's own code), 755 fields.
- 127 distinct opcodes used; **all recognized** by `opcodes.py`.
- Heavy obfuscation: identifiers are single letters, fields overloaded by
  descriptor (class `d` has 272 fields, many sharing names). **Symbol identity
  must be `(owner, name, descriptor)`** everywhere.
- Notable classes: `k` (the `GameCanvas`, 77 methods / 125 fields — the engine),
  `s` (49 methods), `r` (40), `j` (34), `t`/`u` (24 each).

### Runtime contract
The full external API surface lives in [`api-surface.txt`](api-surface.txt).
Summary of what the runtime must implement:

- **java.lang**: Object, String, StringBuffer, Integer, Long, Math, System,
  Class, Runnable, Thread, Runtime, Throwable + IllegalArgumentException,
  RuntimeException, IOException, InterruptedException, Error, Exception.
- **java.io**: ByteArray{In,Out}putStream, Data{Input,Output}Stream, DataInput,
  In/OutputStream, PrintStream.
- **java.util**: Random, Vector.
- **lcdui**: Display, Displayable, Canvas, **GameCanvas** (init/getGraphics/
  flushGraphics), **Graphics** (setColor/fillRect/drawLine/drawRect/drawImage/
  drawRegion/drawRGB/clip/translate), **Image** (createImage from name/stream),
  Alert, AlertType.
- **media**: Manager.createPlayer(InputStream, mime), Player (realize/start/
  setLoopCount/getState/close), VolumeControl.setLevel.
- **midlet**: MIDlet (getAppProperty/notifyDestroyed).
- **rms**: RecordStore (open/close/add/get/set/delete/enumerate/list/count),
  RecordEnumeration, RecordComparator, RecordFilter.

## Architecture

```
tools/recompiler/      Python AOT recompiler (no third-party deps)
  classfile.py         .class parser: const pool, fields, methods, Code
  opcodes.py           full JVM opcode table + variable-length decoder
  jrecomp.py           CLI: `info` (done), `translate` (next)
  translate.py         bytecode -> C codegen                    [TODO]
  layout.py            object layout / vtable / name mangling   [TODO]
runtime/               hand-written J2ME runtime in C
  include/j2me/        public headers (jvm types, object model)
  src/                 lang/io/util + lcdui/media/rms impls
generated/             translator output (one .c/.h per class)  [gitignored]
```

### Translation model (planned)
- **Object model**: every Java object is a heap struct prefixed with a class
  pointer (for vtable dispatch + instanceof/checkcast). Fields laid out by
  `layout.py`, keyed by `(owner, name, descriptor)` to survive overloading.
- **Operand stack → C temporaries.** Each method gets a fresh set of stack-slot
  and local-slot variables typed per JVM verification rules; we recover types
  by a linear abstract interpretation of the stack (the classic stack→register
  recomp move). long/double occupy two slots.
- **Control flow**: one C label per branch target / basic-block leader; branches
  become `goto`. Exception tables become setjmp/longjmp landing pads (the runtime
  carries a per-thread exception-frame stack). `jsr`/`ret` not expected at v45.3
  from this toolchain, but we'll inline subroutines if they appear.
- **Name mangling**: `Owner.name(desc)` → `m_<owner>_<hash>` so overloaded
  members stay distinct and C-legal.
- **GC**: start with a simple arena / conservative mark-sweep; J2ME heaps are
  tiny (the phone had ~1–2 MB). Correctness first, collector later.

## Status / next steps
1. ✅ `.class` parser + opcode table, validated on all 26 classes.
2. ⏭ Object layout + name mangling (`layout.py`).
3. ⏭ Bytecode→C method translator (`translate.py`).
4. ⏭ J2ME runtime: java.lang/io/util first (no I/O surprises), then lcdui on SDL2.
5. ⏭ Boot the MIDlet → EA splash → main menu.

## Bug ledger
Empty for now. Every recomp earns its scars; this is where the
"found-and-fixed" war stories will accumulate (translator mistakes caught by
diffing against a reference JVM run, etc.).
