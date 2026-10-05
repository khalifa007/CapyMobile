// The few C library functions the Java engine uses that a PS5 title does not have, and one it has but
// is better off without.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine_env.h"
#include "log.h"

// A failed assert(): into the log, then stop where a crash report points at it.
void __assert(const char *function, const char *file, int line, const char *expression) {
    log_line("assert: %s failed in %s (%s:%d)", expression, function, file, line);
    __builtin_trap();
}

// Days since 1970 to a calendar date (Howard Hinnant's civil_from_days).
struct tm *gmtime_r(const time_t *when, struct tm *out) {
    int64_t seconds = (int64_t)*when, days = seconds / 86400, rest = seconds % 86400;
    if (rest < 0) { rest += 86400; --days; }
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    int64_t year = (int64_t)yoe + era * 400 + (month <= 2);
    static const int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    *out = (struct tm){0};
    out->tm_sec = (int)(rest % 60);
    out->tm_min = (int)(rest / 60 % 60);
    out->tm_hour = (int)(rest / 3600);
    out->tm_mday = (int)day;
    out->tm_mon = (int)month - 1;
    out->tm_year = (int)(year - 1900);
    out->tm_wday = (int)((days % 7 + 11) % 7);  // 1 January 1970 was a Thursday
    out->tm_yday = before[month - 1] + (int)day - 1 + (leap && month > 2);
    return out;
}

// The console keeps no time zone a title can read: the games get UTC, as a phone with none set gives.
struct tm *localtime_r(const time_t *when, struct tm *out) { return gmtime_r(when, out); }

// The engine reads dozens of NOJME_* debugging switches from the environment, some of them once per
// frame. A title has no environment worth asking, so the answer is "not set" for all but the few
// that Capy Mobile sets itself (engine_env.h). The build names
// this one in place of getenv(): a title may not define a function the console's library also has.
char *cj_getenv(const char *name) {
    for (size_t i = 0; i < sizeof(engine_env) / sizeof(engine_env[0]); ++i)
        if (!strcmp(name, engine_env[i].name)) return (char *)engine_env[i].value;
    return NULL;
}
