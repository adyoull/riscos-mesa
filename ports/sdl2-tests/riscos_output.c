/*
 * riscos_output.c - link this into a program that prints (SDL_Log goes to
 * stderr) and runs in the desktop: printing from a Wimp task opens a
 * command window. Before main() runs, if the variable named by OUTPUT_VAR
 * is set, stdout and stderr go to that file instead. No change to the
 * program's own sources is needed.
 *
 * Build with -DOUTPUT_VAR='"App$Output"'; the !Run file sets it, e.g.
 *   Set App$Output /|<App$Dir>/Output
 * With -DOUTPUT_DEFAULT='"/dev/null"' (say), output goes there when the
 * variable isn't set, so a desktop program never opens a command window.
 *
 * RISC OS lets a file be open for writing only once, so the file is opened
 * once (as stderr) and stdout is made to share it with dup2: opening it a
 * second time fails, and a failed freopen leaves that stream closed (all
 * of stderr was lost that way; found by the Warzone 2100 and Freeciv ports).
 *
 * Part of riscos-mesa. MIT licence (see LICENCES.txt).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef OUTPUT_VAR
#define OUTPUT_VAR "Program$Output"
#endif

__attribute__((constructor))
static void riscos_redirect_output(void)
{
    const char *o = getenv(OUTPUT_VAR);
#ifdef OUTPUT_DEFAULT
    if (!o || !*o)
        o = OUTPUT_DEFAULT;
#endif
    if (!o || !*o || freopen(o, "w", stderr) == NULL)
        return;
    setvbuf(stderr, NULL, _IONBF, 0);
    fflush(stdout);
    if (dup2(fileno(stderr), fileno(stdout)) >= 0)
        setvbuf(stdout, NULL, _IOLBF, 0);
}
