"""Fits the staged copy of the Java engine (noJMe) to the PS5 toolchain.

    python3 port_engine.py <staged engine folder>

build.sh runs it on the copy it stages; the pinned checkout and the plain PC build are not touched.
The console's headers are FreeBSD's, where the engine was written against glibc and, for its
Switch build, newlib. Every replacement must match exactly once, so a new engine commit that moves
one of these places stops the build instead of going out half ported.
"""
import os
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
script_time = Path(__file__).stat().st_mtime

# With _POSIX_C_SOURCE set, FreeBSD's headers hide everything that is not in that standard (usleep,
# M_PI, strcasecmp, gettimeofday...), where glibc's keep showing it. Without it they show everything.
FEATURE = re.compile(rb"^#define _(POSIX_C_SOURCE|DEFAULT_SOURCE)\b[^\n]*\n", re.M)

# (file, old, new)
REPLACEMENTS = [
    # Linux's per-thread "nice": the Switch build already leaves it out, and so does the PS5's.
    ("src/jvm/native.c",
     b"#else\n#include <sys/syscall.h>   /* v34.43: SYS_gettid for per-thread setpriority */",
     b"#elif !defined(CAPY_PS5)\n#include <sys/syscall.h>   /* v34.43: SYS_gettid for per-thread setpriority */"),
    ("src/jvm/native.c",
     b"            setpriority(PRIO_PROCESS, (pid_t)syscall(SYS_gettid), nice_delta);",
     b"#ifndef CAPY_PS5\n            setpriority(PRIO_PROCESS, (pid_t)syscall(SYS_gettid), nice_delta);\n#endif"),
    # The cooperative threads switch with the app's own few instructions (src/ps5/capy_ucontext.h).
    ("src/jvm/threads.c",
     b"#else\n#include <ucontext.h>\n#endif\n#include <sched.h>",
     b"#elif defined(CAPY_UCONTEXT)\n#include \"capy_ucontext.h\"\n#else\n#include <ucontext.h>\n#endif\n#include <sched.h>"),
    # A Java thread of its own gets a 32 MB stack. On the console a thread's stack comes out of the
    # title's 448 MB of flexible memory, which the app's heap shares: 8 MB each there.
    ("src/jvm/native.c",
     b"#define JAVA_NATIVE_STACK_SIZE (32u * 1024u * 1024u)  /* 32 MB */",
     b"#ifdef CAPY_PS5\n#define JAVA_NATIVE_STACK_SIZE (8u * 1024u * 1024u)\n#else\n"
     b"#define JAVA_NATIVE_STACK_SIZE (32u * 1024u * 1024u)  /* 32 MB */\n#endif"),
    # FreeBSD has no "timezone" variable. A phone with no zone set says UTC, and so does the console.
    ("src/jvm/native.c",
     b"    return (long)timezone;  /* glibc/MinGW: SVID global, tzset-backed */",
     b"#ifdef CAPY_PS5\n    return 0;\n#else\n    return (long)timezone;  /* glibc/MinGW: SVID global, tzset-backed */\n#endif"),
]

changed = {}
for path in sorted(root.rglob("*.c")):
    data = path.read_bytes()
    ported = FEATURE.sub(b"", data)
    if ported != data:
        changed[path] = ported

for name, old, new in REPLACEMENTS:
    path = root / name
    data = changed.get(path) or path.read_bytes()
    if data.count(old) != 1:
        sys.exit(f"port_engine: {name}: expected one match, found {data.count(old)}: {old[:60]!r}")
    changed[path] = data.replace(old, new)

for path, data in changed.items():
    # The copy keeps a time that changes only with the engine or with this script, so the build
    # compiles it again only then.
    stamp = max(path.stat().st_mtime, script_time)
    path.write_bytes(data)
    os.utime(path, (stamp, stamp))
print(f"port_engine: {len(changed)} files fitted")
