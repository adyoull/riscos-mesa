#include "hrtime.h"
#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#include <stdint.h>
#include <stddef.h>

#define HAL_CounterRate   19
#define HAL_CounterPeriod 20
#define HAL_CounterRead   21

static int hr_state;                 /* 0 unknown, 1 HAL usable, -1 cs only */
static unsigned int hr_rate, hr_period;
static double hr_last;

static unsigned int monotonic_cs(void)
{
    _kernel_swi_regs r;
    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    return (unsigned int)r.r[0];
}

static int hal(int entry, unsigned int *out)
{
    _kernel_swi_regs r;
    r.r[0] = 0;                      /* HAL counter 0: the centisecond timer */
    r.r[8] = 0;                      /* OS_Hardware 0: call a HAL routine */
    r.r[9] = entry;
    if (_kernel_swi(OS_Hardware, &r, &r) != NULL) return 0;
    *out = (unsigned int)r.r[0];
    return 1;
}

static int hr_init(void)
{
    unsigned int rate, period;
    if (hr_state) return hr_state > 0;
    hr_state = -1;
    if (!hal(HAL_CounterRate, &rate) || !hal(HAL_CounterPeriod, &period)) return 0;
    /* the counter must reload once per centisecond */
    if (rate < 10000 || period == 0 ||
        period < rate / 100 - rate / 10000 - 1 || period > rate / 100 + rate / 10000 + 1)
        return 0;
    hr_rate = rate; hr_period = period; hr_state = 1;
    return 1;
}

double hr_seconds(void)
{
    unsigned int cs1, cs2, cnt = 0;
    double t;
    if (!hr_init()) return monotonic_cs() / 100.0;
    do {
        cs1 = monotonic_cs();
        if (!hal(HAL_CounterRead, &cnt)) { hr_state = -1; return cs1 / 100.0; }
        cs2 = monotonic_cs();
    } while (cs1 != cs2);
    if (cnt > hr_period) cnt = hr_period;
    t = cs1 / 100.0 + (double)(hr_period - cnt) / hr_rate;
    if (t < hr_last) t = hr_last;    /* never go backwards */
    hr_last = t;
    return t;
}

const char *hr_source(void)
{
    return hr_init() ? "HAL counter" : "centiseconds";
}
#else
#include <time.h>
double hr_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
const char *hr_source(void) { return "clock_gettime"; }
#endif
