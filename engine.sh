#!/usr/bin/env bash
# The Java engine Capy Mobile is built on: noJMe (https://github.com/corax89/noJMe, GPL-3.0), pinned to one
# commit and kept outside the sources, in .deps/. With no argument this fetches it if needed and prints
# its folder; "sources" lists the files of its libretro core (relative to that folder), "includes" the
# folders they include from.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
COMMIT=6da353a1bf781f36e10fc52a4e038092d86120e4
deps=${CAPY_MOBILE_DEPS:-$here/.deps}
engine="$deps/noJMe-$COMMIT"
amr=src/amr/opencore/codecs_v2/audio/gsm_amr

if [[ ! -f $engine/.ready ]]; then
    rm -rf "$engine"
    mkdir -p "$deps"
    git -c core.autocrlf=false clone --quiet https://github.com/corax89/noJMe.git "$engine" >&2
    git -C "$engine" -c advice.detachedHead=false checkout --quiet "$COMMIT" >&2
    [[ $(git -C "$engine" rev-parse HEAD) == "$COMMIT" ]] || { echo "the engine is not at the pinned commit" >&2; exit 1; }
    touch "$engine/.ready"
fi

case ${1:-} in
includes)
    echo include src src/include src/jvm src/midp src/utils src/libretro src/amr src/amr/oscl \
        $amr/amr_nb/common/include $amr/amr_nb/dec/include $amr/amr_nb/dec/src $amr/common/dec/include ;;
sources)
    # The lists of the engine's Makefile: ALL_LIBRETRO_SRCS and the AMR decoder.
    cd "$engine"
    ls src/jvm/{classfile,debug_var,execute,heap,jvm,method_cache,native,nokia_m3d,opcodes,sax,stubs,threads,charset,drm_bypass}.c \
       src/midp/{display,form,graphics,media,mobile3d,mascot3d,race_probe,rms}.c src/render/render.c \
       src/utils/{battery,miniz,jar_reader,stb_image_impl,utils}.c src/midi/midi.c \
       src/libretro/{libretro,core_options,sdl_backend_stubs}.c \
       src/amr/amr_nb_dec.c $amr/amr_nb/common/src/*.c $amr/amr_nb/dec/src/*.c ;;
*)
    echo "$engine" ;;
esac
