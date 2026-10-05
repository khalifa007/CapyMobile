# Notes for whoever ports the next thing

What was not obvious while making Capy Mobile, in case it saves someone a day. Everything here was seen
on one PS5 on system software 13.60, with the toolchain commit this repository pins.

## A PS5 title built with ps5-native-app-boilerplate

- **`malloc` is a small system heap.** It runs out long before the process does. `src/ps5/alloc.c`
  takes one 256 MiB block of the title's flexible memory (about 448 MiB in all) and hands it out with
  dlmalloc. The toolchain's build takes definitions but no other compiler flags, so the build defines
  `malloc`, `free`, `calloc`, `realloc`, `strdup` and friends as `cj_...` names for every source.
- **`__thread` does not link.** The toolchain compiles thread-locals as calls to
  `__emutls_get_address()` (`-femulated-tls`) and supplies no such function. `src/ps5/emutls.c` is one.
- **A title may not define a function the console's libraries export.** Defining `getenv` gave
  "native converter does not yet publish application exports". Rename it with a definition instead.
- **Every symbol must be defined, weak ones too.** A weak reference left undefined stops the link.
- **`getdents` needs a buffer of at least one block of the drive.** On `/data` that is 64 KiB; a
  16 KiB read listed nothing. `opendir` is refused in a title, so `getdents` is the way to list.
- **The app's own folder is writable.** `/app0` is `/data/homebrew/<title ID>` mounted by ShadowMount+,
  so games, saves and the log can live there and be reached over FTP.
- **Threads get a 64 KiB stack unless you ask.** The app runs on a 16 MiB thread of its own.
- **Sound:** `sceAudioOutOpen(0xff, 0, 0, 256, 48000, 1)`, the system user, 16-bit stereo.
- **FreeBSD headers:** with `_POSIX_C_SOURCE` defined they hide `usleep`, `M_PI`, `strcasecmp` and
  more, where glibc keeps showing them. There is no `timezone` variable, no `localtime_r`, no `gmtime_r`.
- The console's own `getcontext`/`swapcontext` were not tried: `src/ps5/capy_ucontext.c` is a few
  instructions and runs the same on the PC, where it can be tested.

## noJMe as a libretro core

- **One `retro_init()` and `retro_deinit()` per game.** `retro_unload_game()` only writes the saves;
  the Java machine is taken down by `retro_deinit()`. A second game loaded without it ran on top of the
  first, which kept drawing.
- **The engine parks a game when `retro_run()` stops for 0.3 s.** That is what pauses it under a menu.
  The watch stays armed between games and held the next game's start for 20 s, so `core.c` calls
  `jvm_frontend_pause_enable(0)` around loads and unloads.
- **The "spin breaker".** After 1.5 s of bytecode without one native call the engine throws a
  RuntimeException into the thread, to free games stuck in a loop. A game that unpacks its art in Java
  while loading is interrupted too, and then restarts for ever. `NOJME_SPIN_BREAK_MS` sets the limit
  (`src/engine_env.h` sets 30 s).
- **A jar is built for one screen size.** A wrong one shows as black, cut off, or the game's own
  "turn your phone" note. The engine's headless program with `-w` and `-h` is a quick way to try sizes.
- **The engine explains itself.** `sw_trace_force` and `nojme_wg_report` are optional hooks;
  `src/ps5/hooks.c` sends them to the log: thrown exceptions with class, method and pc, missing
  resources, thread deaths. `NOJME_STALL_DIAG=1` prints the Java stacks when the picture freezes, and
  `javap -p -c` on the game's classes reads the code at those places.
- A `NullPointerException` in `startApp` right at the start is usually harmless: the game asked for
  a property its `.jad` would have had and caught the miss.
