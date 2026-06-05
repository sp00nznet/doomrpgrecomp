# doomrpgrecomp

**Static recompilation of the _Doom RPG_ J2ME engine family (2005–2009) into native Windows executables.**

```
  ▓█████▄  ▒█████   ▒█████   ███▄ ▄███▓    ██▀███   ██▓███    ▄████
  ▒██▀ ██▌▒██▒  ██▒▒██▒  ██▒▓██▒▀█▀ ██▒   ▓██ ▒ ██▒▓██░  ██▒ ██▒ ▀█▒
  ░██   █▌▒██░  ██▒▒██░  ██▒▓██    ▓██░   ▓██ ░▄█ ▒▓██░ ██▓▒▒██░▄▄▄░
  ░▓█▄   ▌▒██   ██░▒██   ██░▒██    ▒██    ▒██▀▀█▄  ▒██▄█▓▒ ▒░▓█  ██▓
  ░▒████▓ ░ ████▓▒░░ ████▓▒░▒██▒   ░██▒   ░██▓ ▒██▒▒██▒ ░  ░░▒▓███▀▒
   ▒▒▓  ▒ ░ ▒░▒░▒░ ░ ▒░▒░▒░ ░ ▒░   ░  ░   ░ ▒▓ ░▒▓░▒▓▒░ ░  ░ ░▒   ▒
   ░ ▒  ▒   ░ ▒ ▒░   ░ ▒ ▒░ ░  ░      ░     ░▒ ░ ▒░░▒ ░       ░   ░
   ░ ░  ░ ░ ░ ░ ▒  ░ ░ ░ ▒  ░      ░        ░░   ░ ░░       ░ ░   ░
     ░        ░ ░      ░ ░         ░         ░                    ░
   ░                                               r e c o m p
```

> Some demons clawed their way out of Phobos. This one clawed its way out of a
> **128×128, 12-button Java phone from 2005** and onto your 4K monitor — without
> an emulator, without a JVM, without `java.exe` anywhere in sight.
>
> We took the obfuscated `.class` files (every field is named `a`, naturally),
> turned the JVM bytecode into C, hand-built the slice of J2ME that the games
> actually touch, and compiled the whole thing into standalone `.exe`s.
> The recompiled C **is** the game. There is no VM underneath. It's demons all
> the way down.
>
> And because every id/Fountainhead phone RPG of that era runs the **same
> engine**, the same recompiler now eats the whole family — Doom RPG, Doom II
> RPG, Wolfenstein RPG, and Orcs & Elves I/II.

![Doom RPG running natively, with the built-in dev/cheat menu docked on top](docs/screenshot.png)

> *The recompiled game booted to its main menu, with our Dear ImGui dev bar
> (File · Debug · Graphics · Audio · Controls) docked above the 128×128 view.*

## Supported games

All built from one codebase via `build.ps1 -Game <key>` (see `tools/games.json`).
Each is its own `.exe` (the obfuscated class names collide across games). Bring
your own legally-obtained JARs.

| Game (`-Game` key) | Year | Native res | Status |
|---|---|---|---|
| **Doom RPG** (`DoomRPG`) | 2005 | 128×150 | ✅ playable |
| **Doom II RPG** (`DoomIIRPG`) | 2009 | 352×416 | ✅ boots → in-game |
| **Doom II RPG SE** (`DoomIIRPG-SE`) | 2009 | 240×320 | ✅ boots → in-game |
| **Wolfenstein RPG** (`WolfRPG`) | 2008 | 240×320 | ✅ boots → in-game |
| **Orcs & Elves** (`OrcsElves`) | 2006 | 240×320 | ✅ boots into the 3D dungeon |
| **Orcs & Elves II** (`OrcsElves2`) | 2007 | 240×320 | ✅ boots to the menu |

> All six share the engine, so they share the runtime: the recompiler reads each
> JAR's `MANIFEST.MF` for its entry MIDlet + screen size, and a per-game
> `game_entry.c` shim decouples the host from any one game. The cheat/debug menu
> is shared too — every title opens its **own** built-in debug menu (God Mode &c.)
> when you slowly press **3 6 6 6** at the title/pause screen, surfaced as a
> one-click action in the dev bar.

---

## Wait, you can "recomp" a Java game?

Yep. The `recomp` family (N64Recomp, gbarecomp, psxrecomp…) takes a binary for a
dead machine and rewrites it as portable C. _Doom RPG_'s "machine" just happens
to be the **Java Virtual Machine** instead of an ARM7 or a MIPS R3000. The JVM is
a stack machine with a tidy, fully-documented instruction set and self-describing
class files — which, frankly, makes it a _friendlier_ recomp target than a pile of
raw ARM with no symbols.

```
┌──────────────┐   ┌───────────────┐   ┌──────────────┐   ┌──────────────┐
│  <game>.jar   │──▶│  Parse .class  │──▶│  Bytecode→C   │──▶│  C source     │
│ 26–35 classes │   │  (const pool,  │   │  per-method   │   │  (.c/.h, one  │
│ +BSP/MIDI/PNG │   │  fields, code) │   │  stack→SSA-ish│   │  file/class)  │
└──────────────┘   └───────────────┘   └──────────────┘   └──────┬───────┘
                                                                  │
        ┌──────────────┐   ┌─────────────┐   ┌──────────────┐    │
        │  <game>.exe   │◀──│  MSVC + SDL2 │◀──│ J2ME runtime  │◀───┘
        │  standalone   │   │   CMake       │   │ (handwritten) │
        └──────────────┘   └─────────────┘   └──────────────┘
```

1. **Parse** — read each `.class`: constant pool, fields, methods, `Code`
   attributes. Java overloads fields by _descriptor_ (six different `a`s in one
   class), so every symbol is keyed by `(owner, name, descriptor)`, never name.
2. **Translate** — each method's stack-based bytecode becomes straight-line C
   operating on an explicit operand-stack model, with `goto` labels for every
   branch target. Types come from descriptors; no guessing.
3. **Runtime** — a hand-written, native sliver of MIDP-2.0 / CLDC-1.0: just the
   classes the game references (see the table below). `Graphics`/`Canvas` blit to
   an SDL2 framebuffer, `RecordStore` saves go to a file, `Player` plays the
   embedded `.mid`s through Windows MIDI.
4. **Compile** — MSVC turns the generated C + runtime into one `.exe`. SDL2 is
   the only runtime dependency.

## The J2ME surface we have to fake

The family only touches this much of the platform — the runtime contract,
enumerated from the constant pools of every game's classes (Doom RPG alone uses
the first five rows; the siblings add `Font`, the Nokia `DirectGraphics` 16-bit
blitter, high-level `Command`/`Form`, and `Connector`/messaging stubs):

| Package | Classes used |
|---|---|
| `java.lang` | Object, String, StringBuffer, Integer, Long, Math, System, Class, Runnable, Thread, Runtime, Throwable + 5 exceptions |
| `java.io` | (Byte)Array{In,Out}putStream, Data{Input,Output}Stream, DataInput, In/OutputStream, PrintStream, IOException |
| `java.util` | Random, Vector |
| `lcdui` | Alert, AlertType, Canvas, Display, Displayable, **Graphics**, **Image**, **game.GameCanvas**, **Font**, Command, Form |
| `lcdui` (Nokia) | **com.nokia.mid.ui.DirectGraphics / DirectUtils** (16-bit `drawPixels`) |
| `media` | Manager, Player, PlayerListener, Control, Controllable, control.VolumeControl |
| `midlet` | MIDlet, MIDletStateChangeException |
| `rms` | RecordStore, RecordComparator, RecordEnumeration, RecordFilter (+ exceptions) |
| `io` (siblings) | Connector, HttpConnection, Connection (offline stubs) |

Mostly that's it. No reflection, no classloaders, no `synchronized` gymnastics
worth worrying about. CLDC **1.0** even means _no floating point_ — the renderer
is all fixed-point integer math (there's a `sintable.bin` in the JAR to prove
it). System text is drawn with one built-in 8×8 bitmap font; the games that ship
their own bitmap font draw it through `Image`. Networking/SMS are stubbed so the
online bits fall back to offline.

## Status

**Doom RPG is playable**; all six games in the family **boot and render** from a
cold start — no JVM, no emulator. Doom RPG walks the textured 3D dungeon; Doom II
RPG / Wolfenstein / Orcs & Elves boot through their intros into gameplay/menus.
Per-game combat tuning and the deeper cheat profiles are the current frontier.

| Component | Status |
|---|---|
| `.class` parser (const pool / fields / methods / code) | ✅ every game (26–35 classes each) |
| Bytecode → C translator | ✅ incl. `wide`, lazy `<clinit>`, own-class inheritance |
| J2ME runtime (lang / io / util / lcdui / media / rms / Nokia / Font) | ✅ shared across all six games |
| Per-game build (`build.ps1 -Game`, registry, manifest-driven entry shim) | ✅ one `.exe` per game |
| Links to a native `DoomRPG.exe` | ✅ zero unresolved symbols |
| SDL2 display + input | ✅ integer-scaled window, keyboard |
| Windows MIDI playback | ✅ via MCI (real General MIDI) |
| **Launches + runs the game loop** | ✅ boots, no crashes, loop alive |
| PNG decode (own inflate, no zlib) | ✅ verified pixel-exact vs reference |
| **Renders the intro** | ✅ copyright → JAMDAT → Fountainhead splashes |
| **Boots to main menu** | ✅ sound prompt → Start Game / Options / Help / Exit |
| **Menu input works** | ✅ d-pad/fire navigate; New Game → the Mars briefing |
| **In-game (3D dungeon)** | ✅ briefing → approach cinematic → textured 3D view + HUD |
| **Movement / turning** | ✅ d-pad turns and walks the first-person view |
| **Dev / cheat menu (ImGui)** | ✅ File·Debug·Graphics·Audio·Controls bar over the game |
| Playable | 🔨 renders + moves; combat / items / maps still to exercise |

### Dev / cheat menu

A Dear ImGui bar docks across the top of the window with the game viewport
below it — shippable as a player cheat/options menu. Because the recomp turns
every game variable into a native C global, the menu pokes the real game state
directly:

- **File** — New Game, plus 9 emulator-style **save states** (full snapshot of
  the bump-arena heap + every static global; restore is a memcpy). Save states
  are game-agnostic and work for every title.
- **Debug** — **"Open built-in debug menu (3-6-6-6)"** drives each game's *own*
  debug menu (God Mode &c.) and works on **all six** with no per-game work. The
  direct pokes (godmode pin / give HP·armor·ammo / **level warp** / **state
  jump**) are driven by a per-game *profile* (`runtime/src/profiles/`); games
  without a decoded profile fall back to the built-in menu (those pokes grey out).
- **Graphics** — window scale, nearest/linear filter, scanline overlay.
- **Audio** — master volume + mute (Windows audio session) and music (MCI).
- **Controls** — rebindable **keyboard + Xbox controller** (left stick drives
  the d-pad), persisted to `controls.cfg`.

See [`docs/`](docs/) for the running design notes and the bug ledger (every
recomp accrues a glorious list of "found-and-fixed" war stories — ours starts
here).

## Quick start

Requires **Visual Studio 2022**, **Python 3**, and **SDL2 + Dear ImGui** via
[vcpkg](https://github.com/microsoft/vcpkg) (`vcpkg install sdl2 imgui`).

```powershell
# 0. Bring your own legally-obtained JAR(s) into game\ (the paths in
#    tools\games.json). Doom RPG is the default; others are -Game keys.
copy path\to\DoomRPG.jar game\

# 1. Recompile bytecode -> C, build the native exe, stage assets + SDL2.dll
.\build.ps1                    # default game (Doom RPG); add -Run to launch
.\build.ps1 -List              # list the supported games + their JAR paths
.\build.ps1 -Game DoomIIRPG -Run   # build + launch another title
#   add -VcpkgRoot <path> if vcpkg isn't at C:\vcpkg

# 2. Raise hell  (finds game\extracted\<game> automatically)
build\DoomRPG.exe
```

`build.ps1` looks the game up in `tools\games.json`, extracts the JAR, runs the
recompiler (`tools\recompiler\jrecomp.py`) into `generated\<game>\`, generates the
save-state registry, compiles the generated C + the shared handwritten runtime
with MSVC, and links a standalone `build\<game>.exe`. The recompiler also keeps a
`runtime_baseline.json` (the union of every game's runtime references) so the
shared runtime always sees the full API surface — regenerate it with
`jrecomp baseline <jars...>` if you extend the runtime.

## Standing on the shoulders of giants

- **[N64Recomp](https://github.com/N64Recomp/N64Recomp)** — proved static recompilation of whole games is practical.
- **gbarecomp / psxrecomp** — the sibling projects this one cribs its structure from.
- **The JVM Spec, Ch. 4 & 6** — the `.class` format and the opcode table, free and exhaustive.
- **[DoomRPG-RE](https://github.com/Erick194/DoomRPG-RE) by Erick194** — a clean C
  reverse-engineering of Doom RPG. We used it purely as a *reference* to decode our
  obfuscated symbols (the player-stat layout behind the cheat menu, the save-store
  names) — **no code was copied** (it's GPL-3.0; this project is MIT). Cross-checking
  its v0.2.1→v0.2.2 fixes against our output also confirmed our J2ME recomp doesn't
  share its BREW-version bugs (e.g. `aiGoal_MOVE` is already correct here).
- **id Software & Fountainhead** — for building one tidy little engine and
  shipping a whole family of great dungeon crawlers on top of it.

## Legal

This repository contains **no** copyrighted game data — no JARs, no bytecode, no
art, no levels. The tooling and runtime are MIT-licensed (see `LICENSE`). _Doom
RPG_, _Doom II RPG_, _Wolfenstein RPG_, and _Orcs & Elves I/II_ are © id Software
/ Electronic Arts / Fountainhead Entertainment. Supply your own legally-obtained
copies.

---

*Built with Claude Code. The recompiled C is the game — no JVM underneath.*
