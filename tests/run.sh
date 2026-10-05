#!/usr/bin/env bash
# Capy Mobile on the PC (Linux or WSL): the app's own code with the simulator in place of the console.
#   bash tests/run.sh <out folder> <home folder with games/> "<script>"     (sim.c explains the script)
# The engine is fetched by engine.sh and built here once, as a library. Example, with the Snake that
# comes with the app:
#   mkdir -p /tmp/home/games && cp games/snake.jar /tmp/home/games/
#   bash tests/run.sh /tmp/shots /tmp/home "w20 s:list X w120 R w40 D w40 s:game OPT w10 s:menu"
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$here/.." && pwd)
[[ $# -ge 3 ]] || { echo "usage: run.sh <out folder> <home folder> \"<script>\"" >&2; exit 2; }
out=${CAPY_MOBILE_BUILD:-$repo/.deps/pc-build}
engine=$(bash "$repo/engine.sh")
mkdir -p "$out/obj" "$1"
cc=${CC:-clang-18}
sanitize=${SANITIZE:-address,undefined}

includes=""
for path in $(bash "$repo/engine.sh" includes); do includes="$includes -I$engine/$path"; done

# PORTED=1 builds the engine as the console gets it: a copy fitted by port_engine.py, switching its
# threads with the app's own context switch instead of the C library's. Both machines are x86-64, so
# this is the code the PS5 runs.
extra=""
if [[ -n ${PORTED:-} ]]; then
    ported="$out/engine-ported"
    rm -rf "$ported"
    mkdir -p "$ported"
    cp -pr "$engine/include" "$engine/src" "$ported/"
    python3 "$repo/port_engine.py" "$ported" >&2
    includes="${includes//$engine/$ported} -I$repo/src/ps5 -DCAPY_UCONTEXT=1"
    engine=$ported
    extra="$repo/src/ps5/capy_ucontext.c"
fi

# The engine keeps its own flags and gets no sanitizer: it is 200,000 lines of someone else's code.
# ENGINE_SANITIZE=1 builds it with one, to look for its faults.
engine_flags="-std=c11 -O2 -g -w -D_GNU_SOURCE -DLIBRETRO -DJ2ME_DEBUG=0 $includes"
[[ -z ${ENGINE_SANITIZE:-} ]] || engine_flags="$engine_flags -fsanitize=$sanitize"
library="$out/engine.$(echo "$cc $engine_flags $(sha256sum < "$repo/port_engine.py")" | sha256sum | cut -c1-12).a"
if [[ ! -f $library ]]; then
    rm -rf "$out/engine-obj"
    mkdir -p "$out/engine-obj"
    n=0
    for source in $(bash "$repo/engine.sh" sources); do
        n=$((n + 1))
        echo "$cc $engine_flags -c $engine/$source -o $out/engine-obj/$n.o"
    done > "$out/engine-obj/commands"
    xargs -P "$(nproc)" -I{} sh -c '{}' < "$out/engine-obj/commands"
    ar rcs "$library" "$out"/engine-obj/*.o
fi

printf '#define CAPY_MOBILE_VERSION "sim"\n#define CAPY_MOBILE_TITLE "PPSA92280"\n' > "$out/app_version.h"
flags="-std=c11 -O1 -g -Wall -Wextra -D_GNU_SOURCE -fsanitize=$sanitize -I$out -I$repo/src -I$repo/third_party -I$engine/include"
$cc $flags -Dmain=app_main -c "$repo/src/app.c" -o "$out/obj/app.o"
$cc $flags "$here/sim.c" "$out/obj/app.o" "$repo/src/core.c" "$repo/src/audio.c" "$repo/src/log.c" "$repo/src/ps5/hooks.c" $extra "$library" \
    -lpthread -lm -o "$out/sim"
CAPY_MOBILE_HOME="$2" CAPY_MOBILE_WAV="$1/sound.wav" "$out/sim" "$1" "$3"
