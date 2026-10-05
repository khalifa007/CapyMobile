#pragma once
// The engine's own switches that Capy Mobile sets. The engine reads them from the environment
// (NOJME_...); a PS5 title has none, so the console build answers getenv() from this table
// (src/ps5/libc_port.c) and the PC build puts the same values into its environment (app.c).
static const struct { const char *name, *value; } engine_env[] = {
    // The engine takes 1.5 s of Java without a single call into the phone's library for a game stuck
    // in a loop and throws an exception into it. A game that unpacks its pictures in Java while it
    // loads does just that for much longer: Assassin's Creed 2 took 10 s, was interrupted and started
    // over, for ever. 30 s still frees a game that really is stuck.
    {"NOJME_SPIN_BREAK_MS", "30000"},
};
