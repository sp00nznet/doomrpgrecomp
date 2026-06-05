# Per-game cheat profiles

The dev menu's **direct** cheats (godmode pin, give HP/armor/ammo, level warp,
state jump, live stat readouts) poke the game's own globals. Those globals are
obfuscated and **differ per game**, so each title supplies a `GameProfile`
(`runtime/include/gameprofile.h`) in `runtime/src/profiles/profile_<game>.c`,
selected by the `profile` key in `tools/games.json`.

- **Doom RPG** has a complete profile (`profile_DoomRPG.c`), decoded against the
  DoomRPG-RE reference.
- Every other game currently uses **`profile_stub.c`** (all-NULL): the direct
  pokes grey out, but the generic dev menu still works — save states, graphics/
  audio/controls, and crucially the engine's **own built-in debug menu** (God
  Mode &c.), opened on any title via the dev bar's *"Open built-in debug menu
  (3-6-6-6)"* (it injects the 3,6,6,6 key sequence the engine listens for).

So cheating works on all six games today via 3-6-6-6; a profile just adds the
one-click direct pokes.

## Writing a profile for a new game

Profiles are only worth shipping when **verified** — a wrong address write
corrupts the heap or crashes. Don't guess. Use the built-in discovery tool:

1. **Dump int statics.** Run the game; press **F8** at a known moment to print
   every `jint` static (`name = value`) to stderr (the registry carries names —
   see `tools/gen_savestate.py` → `g_static_info`). Redirect stderr to a file.
2. **Diff two known states.** Snapshot at, e.g., the main menu and again in a
   fight at full health; `diff` the dumps. The **state machine** global is a
   small int that takes distinct values per screen; **health/armor/ammo** are
   found by snapshotting before/after taking damage or picking items up and
   seeing which value moves by the right amount.
3. **Find the methods.** Health/armor in this engine usually live on a combat
   object reached via a static ref (Doom RPG: `S_j__a__Lt`, with getters
   `a()/b()/c()/d()` and setters `a(I)/c(I)`). Confirm the accessor descriptors
   in the generated `<class>.c`.
4. **Fill in `profile_<game>.c`** (copy `profile_DoomRPG.c`), add a `profile`
   entry to `tools/games.json`, rebuild, and re-verify each cheat in-game.

Because the games share the engine, the *shapes* (a combat object with a getter
quad, a state int, a level-load method) recur — only the obfuscated names change.
