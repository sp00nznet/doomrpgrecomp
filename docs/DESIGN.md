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

### Decided
- **Saves**: `RecordStore`s persist as `<storename>.rms` files next to the exe.
- **Window**: integer-scaled (nearest-neighbour) from the native 128×128, resizable.
- **Opcodes actually used** (relevant hard ones): `tableswitch`×30, `lookupswitch`×7,
  `invokeinterface`×16, `athrow`×10 + exception tables, `monitorenter/exit`,
  `checkcast`×6, `multianewarray`×1. **No** `jsr`/`ret`/`wide`/`invokedynamic`.
- **Threads/monitors**: the game uses a worker `Thread`; v1 runs cooperatively and
  treats `monitorenter`/`monitorexit` as no-ops (revisit if it deadlocks).

## Status / next steps
1. ✅ `.class` parser + opcode table, validated on all 26 classes.
2. ✅ Name mangling (`mangle.py`) + object layout (`layout.py`): offset-based
   fields, storage-typed statics, per-class method tables + jclass metadata.
3. ✅ Bytecode→C translator (`translate.py`): explicit stack-slot model with an
   abstract-interpretation typing pass; all 383 methods translate and the full
   generated tree **compiles clean under MSVC** (compile-only; no runtime yet).
4. ⏭ J2ME runtime bodies: java.lang/io/util first (no I/O surprises), then
   lcdui on SDL2. 129 runtime methods / 21 classes / 2 statics to implement
   (the exact list falls out of `jrecomp translate`).
5. ✅ Runtime implemented (all 129 methods), links to a native `DoomRPG.exe`,
   launches, and **runs the game loop without crashing**. SDL2 window + keyboard,
   real MIDI via MCI, `.rms` saves, asset loading from the extracted JAR.
6. ✅ PNG decode (`png.c`, hand-written DEFLATE inflate + filters, palette/RGB/
   gray, tRNS) — verified pixel-exact against a Python reference. The game now
   renders its intro splashes (copyright text, JAMDAT, Fountainhead).
7. ✅ Main menu reached. `drawImage`/`drawRegion` now honour the MIDP anchor and
   the 8 Sprite transforms (`lcdui.c`), so the intro splashes centre correctly and
   the "Enable sounds?" boot prompt + DOOM RPG logo land where they should.
   Debug aids: `DOOMRPG_DUMP=path.ppm` dumps the framebuffer each flush;
   `DOOMRPG_DUMPDIR=dir` dumps a numbered boot sequence (stride `DOOMRPG_DUMPN`,
   cap `DOOMRPG_DUMPMAX`) — invaluable for catching the menu without a key in hand.
8. ✅ Keyboard input drives the menus: sound prompt → main menu (Start Game /
   Options / Help / Exit) → New Game → the Mars briefing crawl. Two fixes landed:
   `getGameAction` now maps device keys to MIDP action constants, and the screen
   is 128x150 so the engine's 128x128 view isn't clipped (see ledger). A scripted
   input harness (`DOOMRPG_KEYS="fire@17000,..."`) drives the menus headlessly.
9. ✅ Into the first level. Past the briefing the Mars approach cinematic plays,
   then the textured first-person view renders with the HUD strip (health/armour/
   ammo + Menu/Map soft labels) below it; the d-pad turns and walks. The BSP
   renderer, texture mapping and sprite blitting all came up with no extra changes
   once input + screen size were fixed — verified headlessly via `DOOMRPG_KEYS`.
10. ⏭ Exercise gameplay depth: combat (firing, enemies), item/inventory use, the
   automap, doors/level transitions, and save/restore via the `.rms` store.

### Runtime implementation notes
- Dispatch: runtime singletons (Runtime/Display) and wrapper objects must carry a
  real `jclass` whose table holds the virtually-called methods — booting was a
  sequence of "give class X its method table" fixes (Runtime, Display, MIDlet…).
- Threads: `Thread.start()` runs `run()` inline; `Thread.sleep()` pumps SDL.
- Exceptions classes carry the real super chain (RuntimeException⊂Exception, …)
  so the translator's `j_instanceof`-based catch dispatch matches Java.
- MIDI: SMF bytes spilled to a temp `.mid`, played via Windows MCI.
- Build: `build.ps1` (recompile → cl → link → stage assets+SDL2.dll).

### Translator implementation notes
- Stack entries are single-tagged (`i/l/f/d/a`); long/double are one entry
  (category 2). Vars: `st<idx>_<tag>` (stack), `loc<slot>_<tag>` (locals).
- Labels emitted only at real jump targets (not fall-through) to keep C clean.
- `dup`/`swap` family realized via a temp-snapshot block — correct even when
  adjacent slots share a tag.
- Virtual/interface dispatch = runtime `j_vfind(cls, name, desc)` search (simple
  + correct; real vtables can come later if perf ever matters).
- Exceptions: per-method `j_try` setjmp pad + a `_pc` cursor; handler table
  scanned in order, `j_instanceof` match → `goto` handler, else `j_rethrow`.
- Field access is offset-based via `J_x(obj, OFFEXPR)`; offsets are
  `J2ME_BASE_<super> + N` so the runtime owns base-class layout.

## Bug ledger
Every recomp earns its scars; this is where the "found-and-fixed" war stories
accumulate (translator mistakes caught by diffing against a reference JVM run, etc.).

- **Splashes/logo drawn into the bottom-right; menu logo half off-screen.**
  `Graphics.drawImage`/`drawRegion` ignored the MIDP `anchor` argument and always
  drew top-left. The game routes its blits through a `k` helper that forwards the
  anchor straight to `drawRegion` (`k.c:9671`); the intro passes `anchor=3`
  (`HCENTER|VCENTER`) with the destination point at screen centre, so a 128×128
  image got its *top-left* at (64,64) → visible only in the bottom-right quadrant.
  Fix: `anchor_origin()` converts the anchor point to a top-left origin, and
  `blit_xform()` now also applies the 8 Sprite transforms (the swap of on-screen
  w/h for the rot90/270 family feeds the anchor maths). `lcdui.c`.
  *Symptom that pointed here: intro logos in the corner, DOOM RPG title clipped.*

- **Every menu keypress was a no-op.** `Canvas.getGameAction()` returned the raw
  key code unchanged ("the display layer already reports game actions" — it does
  not). The game's `k.a(I)I` calls `getGameAction()` and switches on the *MIDP*
  action constants (UP=1/DOWN=6/LEFT=2/RIGHT=5/FIRE=8), so every direction/fire
  fell through to "no action" and the menus never moved. Fix: map our device key
  codes (display.c's `KEY_UP=-1…KEY_FIRE=-5`) to those constants. `lcdui.c`.
  *Found by scripting a fire at the sound prompt and seeing nothing happen.*

- **Top ~11px of every screen clipped.** We modelled the Canvas as 128x128, but
  the engine renders a 128x128 view and reserves a HUD strip *below* it, deriving
  the view height as `canvasHeight-22`. With a 128 canvas the view became 106 tall
  and its centre `S_k.i = e/2 = 53`; the briefing/3D code then centres 128x128
  boxes on that centre (`y = centre-64 = -11`), so the top 11px fell off-screen.
  The real full-screen device is **128x150** (150-22 = the 128 view the engine
  hardcodes). Fix: `SCREEN_H = 150` (`runtime.h`); the splash/menu/briefing now
  sit correctly with the HUD strip below. *Symptom: "top of the screen cut off".*
