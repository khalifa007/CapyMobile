# Third-party code

## In this repository

| File | What | Licence |
|---|---|---|
| `third_party/dlmalloc.c` | Doug Lea's malloc 2.8.6, unchanged (from https://gee.cs.oswego.edu/pub/misc/malloc-2.8.6.c, SHA-256 `103602c3fcbe200d5e257cdd7353d84bcc033d887bea3b245321319bf5401f47`). The app's heap on the console. | Public domain (CC0) |
| `third_party/stb_image_write.h` | stb_image_write 1.16 by Sean Barrett. Only the PC simulator uses it, to save PNGs. | Public domain or MIT |

## Fetched when building, not included

| What | Where | Pinned | Licence |
|---|---|---|---|
| noJMe, the J2ME engine | https://github.com/corax89/noJMe | commit `6da353a1bf781f36e10fc52a4e038092d86120e4` | GPL-3.0 |
| ps5-native-app-boilerplate, the toolchain | https://github.com/blackbearreloaded/ps5-native-app-boilerplate | commit `dd44bbdc75437332ed22e3ba95126733419ef25a` | GPL-3.0-or-later |
| ps5-payload-sdk | fetched by the toolchain | the toolchain's pin | see the SDK |

The engine carries code of its own: miniz, stb_image, minimp3 and the opencore AMR decoder
(Apache-2.0). They are linked into the app; their notices are in the engine's sources.

`port_engine.py` changes a copy of the engine at build time (see the comments in it): it removes
feature-test defines that hide functions in the console's headers, leaves out a Linux-only call,
plugs in this repository's context switch, and gives Java threads smaller stacks. A built Capy Mobile
therefore contains noJMe with those changes.
