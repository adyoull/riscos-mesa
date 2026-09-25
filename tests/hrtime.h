/* hrtime - high resolution monotonic time for benchmarks.
   RISC OS: the centisecond monotonic timer plus the HAL counter that
   generates its tick (same method as the riscos-openttd UnixLib patch),
   so it doesn't need a patched UnixLib. Elsewhere: clock_gettime. */
#ifndef HRTIME_H
#define HRTIME_H
double hr_seconds(void);          /* monotonic, seconds */
const char *hr_source(void);      /* "HAL counter", "centiseconds" or "clock_gettime" */
#endif
