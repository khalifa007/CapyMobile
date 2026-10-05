#!/usr/bin/env bash
# Builds Capy Mobile as a native PS5 title, on Linux or in WSL.
#
#   bash build.sh
#
# Needs git, python3, clang-18, lld-18, ninja and make (the toolchain's own set-up says what is missing).
# Everything else is fetched once into .deps/:
#   the toolchain   blackbearreloaded/ps5-native-app-boilerplate at a pinned commit (it downloads the
#                   open ps5-payload-sdk itself). PS5_TOOLCHAIN=<folder> uses a checkout you already have.
#   the engine      noJMe at a pinned commit (engine.sh)
# The toolchain compiles every source under one folder of its own tree, so the sources are staged there:
# src/ and src/ps5/, and the engine's libretro core after port_engine.py has fitted it to the console.
# Output: dist/<TITLE ID>/, the folder to copy to /data/homebrew/<TITLE ID> on the PS5 (deploy.py).
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
deps=${CAPY_MOBILE_DEPS:-$here/.deps}
TOOLCHAIN=https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git
TOOLCHAIN_COMMIT=dd44bbdc75437332ed22e3ba95126733419ef25a

tool=${PS5_TOOLCHAIN:-$deps/ps5-native-app-boilerplate}
if [[ ! -f $tool/tools/build.sh ]]; then
    [[ -z ${PS5_TOOLCHAIN:-} ]] || { echo "PS5 toolchain not found at $tool" >&2; exit 2; }
    mkdir -p "$deps"
    git -c core.autocrlf=false clone --quiet "$TOOLCHAIN" "$tool"
    git -C "$tool" -c advice.detachedHead=false checkout --quiet "$TOOLCHAIN_COMMIT"
fi
tool=$(cd -- "$tool" && pwd)
engine=$(bash "$here/engine.sh")

# Stage. Copies keep their times, so only changed files are compiled again.
stage="$tool/capymobile"
rm -rf "$stage"
mkdir -p "$stage/src/engine" "$stage/sce_sys"
cp -p "$here"/src/*.c "$here"/src/*.h "$here"/src/ps5/* "$stage/src/"
cp -p "$here/third_party/dlmalloc.c" "$stage/src/dlmalloc.inc"
# The engine's tree as it is, then without the sources that are not part of its libretro core (its
# SDL and Switch frontends and its own main): the build would compile anything it finds.
cp -pr "$engine/include" "$engine/src" "$stage/src/engine/"
keep=$(bash "$here/engine.sh" sources)
(cd "$stage/src/engine" && find . -name '*.c' | sed 's|^\./||' | grep -vxF -f <(echo "$keep") | xargs -r rm -f)
python3 "$here/port_engine.py" "$stage/src/engine"
cp -p "$here"/sce_sys/* "$stage/sce_sys/"

version=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["contentVersion"])' "$here/sce_sys/param.json")
title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$here/sce_sys/param.json")
mkdir -p "$deps"
printf '#define CAPY_MOBILE_VERSION "%s"\n#define CAPY_MOBILE_TITLE "%s"\n' "$version" "$title" > "$deps/app_version.h.new"
cmp -s "$deps/app_version.h.new" "$deps/app_version.h" 2>/dev/null || cp "$deps/app_version.h.new" "$deps/app_version.h"
rm "$deps/app_version.h.new"
cp -p "$deps/app_version.h" "$stage/src/app_version.h"

includes="capymobile/src"
for path in $(bash "$here/engine.sh" includes); do includes="$includes capymobile/src/engine/$path"; done

# The toolchain's build takes no compiler flags, only definitions, so those do the porting: the cj_
# names send every allocation of the app and the engine to the app's own heap (src/ps5/alloc.c), and
# getenv() to the app's table (src/ps5/libc_port.c).
cd "$tool"
APP_SOURCE_DIR=capymobile/src APP_PARAM=capymobile/sce_sys/param.json APP_SCE_SYS=capymobile/sce_sys APP_ASSETS= \
APP_INCLUDE_PATHS="$includes" \
APP_DEFINITIONS="CAPY_PS5=1 CAPY_UCONTEXT=1 LIBRETRO=1 _GNU_SOURCE=1 J2ME_DEBUG=0 malloc=cj_malloc free=cj_free calloc=cj_calloc realloc=cj_realloc strdup=cj_strdup strndup=cj_strndup posix_memalign=cj_posix_memalign aligned_alloc=cj_aligned_alloc getenv=cj_getenv" \
    bash tools/build.sh Folder

out="$here/dist"
rm -rf "$out/$title"
mkdir -p "$out"
cp -r "$tool/dist/$title" "$out/"
mkdir -p "$out/$title/games" "$out/$title/saves"
(cd "$out" && rm -f "CapyMobile-$version.zip" && python3 -m zipfile -c "CapyMobile-$version.zip" "$title")
echo "Capy Mobile $version: $out/$title (and CapyMobile-$version.zip)"
