/* SPDX-License-Identifier: MIT */
#ifndef SYSRIBBON_PRESENTATION_H
#define SYSRIBBON_PRESENTATION_H

#include "sampler.h"
#include <stdbool.h>
#include <stddef.h>

#define SR_HISTORY_CAP 48
#define SR_SECTION_COUNT 4

enum { SR_SECTION_CPU, SR_SECTION_RAM, SR_SECTION_TEMP, SR_SECTION_NETWORK };

typedef struct {
    double data[SR_HISTORY_CAP];
    unsigned next, count;
} SrHistory;

typedef struct {
    bool colors, graphs, minimal;
    unsigned points;
    double warning[3], critical[3];
    /* Per-metric graph floors: CPU and RAM use %, temperature uses °C. */
    double floor[3];
} SrDisplayOptions;

void sr_history_add(SrHistory *h, bool valid, double value);
/* All formatted output is bounded, valid Pango markup. No GTK/GLib dependency. */
void sr_format_section(char *dst, size_t cap, unsigned section,
                       const SrSample *sample, const SrHistory *history,
                       const SrDisplayOptions *opts, bool separator);

#endif
