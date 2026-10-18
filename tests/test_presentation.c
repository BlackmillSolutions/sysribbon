/* SPDX-License-Identifier: MIT */
#include "../src/presentation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void contains(const char *str, const char *needle) {
    if (!strstr(str, needle)) {
        fprintf(stderr, "Missing %s from %s\n", needle, str);
        assert(0);
    }
}

int main(void) {
    SrDisplayOptions options = {.colors=true, .graphs=true, .points=8,
        .warning={50,70,65}, .critical={80,90,85}};
    SrSample sample = {.cpu_ok=true, .cpu_pct=83.0, .ram_ok=true,
        .ram_pct=42, .ram_total_gib=16, .ram_used_gib=6.8,
        .temp_ok=true, .temp_c=72, .network_ok=true,
        .download_bps=1400000, .upload_bps=12345};
    SrHistory history = {0};
    char output[1024];
    sr_history_add(&history, true, 25);
    sr_history_add(&history, true, 55);
    sr_history_add(&history, false, 100);
    sr_history_add(&history, true, NAN);
    assert(history.count == 2);
    sr_format_section(output, sizeof(output), SR_SECTION_CPU, &sample, &history, &options, false);
    contains(output, "#ff5555");
    contains(output, "▂");
    contains(output, "83%");
    sr_format_section(output, sizeof(output), SR_SECTION_TEMP, &sample, &history, &options, true);
    contains(output, "#f1fa8c");
    contains(output, "°C");
    contains(output, "│");

    options.colors = false;
    sr_format_section(output, sizeof(output), SR_SECTION_CPU, &sample, &history, &options, false);
    assert(!strstr(output, "foreground="));
    contains(output, "▂");
    options.graphs = false;
    sr_format_section(output, sizeof(output), SR_SECTION_CPU, &sample, &history, &options, false);
    assert(!strstr(output, "▂"));
    assert(!strstr(output, "·"));
    contains(output, "CPU 83%");
    sr_format_section(output, sizeof(output), SR_SECTION_NETWORK, &sample, NULL, &options, true);
    assert(!strstr(output, "foreground="));
    contains(output, "MiB/s");
    contains(output, "KiB/s");

    /* Minimal mode suppresses names and history regardless of graph toggle. */
    options.minimal = true;
    options.graphs = true;
    sr_format_section(output, sizeof(output), SR_SECTION_CPU, &sample, &history, &options, false);
    assert(!strstr(output, "CPU"));
    assert(!strstr(output, "▂"));
    contains(output, "83%");
    sr_format_section(output, sizeof(output), SR_SECTION_RAM, &sample, &history, &options, false);
    assert(!strstr(output, "RAM"));
    contains(output, "6.8/16.0GiB");
    sr_format_section(output, sizeof(output), SR_SECTION_TEMP, &sample, &history, &options, false);
    assert(!strstr(output, "TEMP"));
    contains(output, "72°C");

    options.colors = true;
    sr_format_section(output, sizeof(output), SR_SECTION_NETWORK, &sample, NULL, &options, false);
    contains(output, "#8be9fd");

    sample.network_ok = false;
    sr_format_section(output, sizeof(output), SR_SECTION_NETWORK, &sample, NULL, &options, false);
    contains(output, "--");

    for (size_t n = 1; n <= 32; ++n) {
        char tiny[40];
        memset(tiny, 'Z', sizeof(tiny));
        sr_format_section(tiny, n, SR_SECTION_RAM, &sample, &history, &options, false);
        assert(tiny[n - 1] == '\0' || strlen(tiny) < n);
        assert(tiny[n] == 'Z');
    }
    puts("OK presentation: color, snapshot, minimal, history, speed, bounded formatting");
    return 0;
}
