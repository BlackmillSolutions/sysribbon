/* SPDX-License-Identifier: MIT */
#include "../src/sampler.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

int main(void) {
    SrSampler sampler;
    SrSample a, b;
    sr_sampler_init(&sampler);
    sr_sample(&sampler, &a);
    assert(a.ram_ok);
    assert(a.ram_pct >= 0 && a.ram_pct <= 100);
    assert(a.ram_used_gib <= a.ram_total_gib);
    assert(!a.cpu_ok); /* first reading establishes baseline */
    struct timespec pause_for = { .tv_sec = 0, .tv_nsec = 150000000L };
    nanosleep(&pause_for, NULL);
    sr_sample(&sampler, &b);
    assert(b.cpu_ok);
    assert(b.cpu_pct >= 0 && b.cpu_pct <= 100);
    if (b.temp_ok) assert(isfinite(b.temp_c) && b.temp_c >= -20 && b.temp_c <= 125);
    printf("OK CPU %.1f%% RAM %.1f%% TEMP %s NET %s\n",
        b.cpu_pct, b.ram_pct, b.temp_ok ? "available" : "unavailable",
        b.network_ok ? "available" : "unavailable");
    return 0;
}
