/* SPDX-License-Identifier: MIT */
#include "../src/presentation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void require(const char *haystack, const char *needle) {
    if (!strstr(haystack, needle)) {
        fprintf(stderr, "Expected [%s] in output: %s\n", needle, haystack);
        assert(0);
    }
}

static void check_bar(unsigned index, double observed, double floor,
                      const char *expected_bar) {
    SrDisplayOptions options = {
        .graphs = true, .colors = false, .points = 4,
        .warning = {50, 70, 65}, .critical = {80, 90, 85},
        .floor = {0, 0, 30}
    };
    options.floor[index] = floor;
    SrHistory history = {0};
    sr_history_add(&history, true, observed);
    SrSample sample = {0};
    sample.cpu_ok = sample.ram_ok = sample.temp_ok = true;
    sample.cpu_pct = sample.ram_pct = sample.temp_c = observed;
    sample.ram_total_gib = 4;  /* Also works on low-memory machines. */
    sample.ram_used_gib = sample.ram_total_gib * observed / 100;
    char output[1024];
    sr_format_section(output, sizeof(output), index, &sample, &history, &options, false);
    require(output, expected_bar);
}

int main(void) {
    /* CPU and RAM use % for scale, independent of the actual GiB amount. */
    check_bar(SR_SECTION_CPU, 75, 0, "▆");
    check_bar(SR_SECTION_CPU, 75, 50, "▄");
    check_bar(SR_SECTION_RAM, 80, 0, "▆");
    check_bar(SR_SECTION_RAM, 80, 50, "▅");

    /* Temperature floor defaults to 30 °C; can be changed freely. */
    check_bar(SR_SECTION_TEMP, 50, 30, "▃");
    check_bar(SR_SECTION_TEMP, 50, 40, "▂");
    check_bar(SR_SECTION_TEMP, 15, 30, "▁");

    /* Above 100 °C, auto upper bound grows: 108 °C does not clip. */
    check_bar(SR_SECTION_TEMP, 108, 30, "▇");

    /* Max range stays physically bounded for percent metrics. */
    check_bar(SR_SECTION_CPU, 100, 50, "█");
    check_bar(SR_SECTION_RAM, 100, 50, "█");
    puts("OK scales: adjustable CPU/RAM/temp floors and automatic upper limits");
    return 0;
}
