// Three optional hooks the Java engine leaves to its frontend (its Switch one fills them in). On
// Linux they may simply be absent; a PS5 title must define every symbol it names, and two of them are
// worth having anyway: they are the engine's own reports of trouble, and here they go into the app's log.
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

// At most this many lines per launch: a game that trips a guard every frame must not fill the log.
#define MOST 300
static atomic_int lines;

void sw_trace_force(const char *format, ...) {
    if (atomic_fetch_add(&lines, 1) >= MOST) return;
    char line[400];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
    log_line("engine: %s", line);
}

// The engine's guard against a wild pointer caught one before it was followed.
void nojme_wg_report(const char *site, const void *value) {
    if (atomic_fetch_add(&lines, 1) >= MOST) return;
    log_line("engine: a bad pointer was stopped at %s (%p)", site ? site : "?", value);
}

// What a game is told when it asks for the phone's language (microedition.locale).
const char *switch_locale_get(void) { return "en-US"; }
