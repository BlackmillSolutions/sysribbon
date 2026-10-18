/* SPDX-License-Identifier: MIT */
#include "presentation.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define GREEN "#50fa7b"
#define YELLOW "#f1fa8c"
#define RED "#ff5555"
#define BLUE "#8be9fd"
#define MUTED "#909090"

typedef struct {
    char *buf;
    size_t size, used;
} Writer;

static void append(Writer *w, const char *format, ...) {
    if (w->used >= w->size) return;
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(w->buf + w->used, w->size - w->used, format, ap);
    va_end(ap);
    if (n < 0) return;
    size_t written = (size_t)n;
    w->used += written < w->size - w->used ? written : w->size - w->used - 1;
}

void sr_history_add(SrHistory *h, bool valid, double value) {
    if (!valid || !isfinite(value)) return;
    h->data[h->next] = value;
    h->next = (h->next + 1) % SR_HISTORY_CAP;
    if (h->count < SR_HISTORY_CAP) ++h->count;
}

static const char *color_at(bool valid, double value, double warning, double critical) {
    if (!valid) return MUTED;
    if (value >= critical) return RED;
    if (value >= warning) return YELLOW;
    return GREEN;
}

static void speed_string(char *dst, size_t cap, double bytes_per_sec) {
    if (!isfinite(bytes_per_sec) || bytes_per_sec < 0) {
        snprintf(dst, cap, "--");
    } else if (bytes_per_sec < 1024.0) {
        snprintf(dst, cap, "%.0fB/s", bytes_per_sec);
    } else if (bytes_per_sec < 1048576.0) {
        snprintf(dst, cap, "%.0fKiB/s", bytes_per_sec / 1024.0);
    } else if (bytes_per_sec < 1073741824.0) {
        snprintf(dst, cap, "%.1fMiB/s", bytes_per_sec / 1048576.0);
    } else {
        snprintf(dst, cap, "%.1fGiB/s", bytes_per_sec / 1073741824.0);
    }
}

void sr_format_section(char *dst, size_t cap, unsigned section,
                       const SrSample *s, const SrHistory *hist,
                       const SrDisplayOptions *opts, bool separator) {
    if (!cap) return;
    Writer w = {dst, cap, 0};
    dst[0] = '\0';
    if (section >= SR_SECTION_COUNT || !s || !opts) return;

    append(&w, "<span font_family='monospace'>");
    if (separator) {
        if (opts->colors) append(&w, "<span foreground='%s'>│</span> ", MUTED);
        else append(&w, "│ ");
    }

    if (section == SR_SECTION_NETWORK) {
        char down[32] = "--", up[32] = "--";
        if (s->network_ok) {
            speed_string(down, sizeof(down), s->download_bps);
            speed_string(up, sizeof(up), s->upload_bps);
        }
        if (opts->colors) append(&w, "<span foreground='%s'>", BLUE);
        /* Fixed-width fields + a fixed-width GTK section: no panel jitter on unit changes. */
        append(&w, "↓%9s ↑%9s", down, up);
        if (opts->colors) append(&w, "</span>");
        append(&w, "</span>");
        return;
    }

    bool ok = false;
    double value = 0.0;
    const char *name = "";
    char text[64];
    if (section == SR_SECTION_CPU) {
        name = "CPU"; ok = s->cpu_ok; value = s->cpu_pct;
        snprintf(text, sizeof(text), ok ? "%.0f%%" : "--%%", value);
    } else if (section == SR_SECTION_RAM) {
        name = "RAM"; ok = s->ram_ok; value = s->ram_pct;
        if (ok) snprintf(text, sizeof(text), "%.1f/%.1fGiB", s->ram_used_gib, s->ram_total_gib);
        else snprintf(text, sizeof(text), "--GiB");
    } else {
        name = "TEMP"; ok = s->temp_ok; value = s->temp_c;
        snprintf(text, sizeof(text), ok ? "%.0f°C" : "--°C", value);
    }

    if (opts->colors)
        append(&w, "<span foreground='%s'>",
               color_at(ok, value, opts->warning[section], opts->critical[section]));
    if (!opts->minimal) append(&w, "%s ", name);
    if (opts->graphs && !opts->minimal && hist) {
        /* No upper-limit setting is necessary: percentages always peak at 100.
         * Temperatures use 100 °C normally and extend upward only when
         * samples actually exceed 100 °C. Read history once per redraw. */
        double min = opts->floor[section];
        if (!isfinite(min)) min = section == SR_SECTION_TEMP ? 30.0 : 0.0;
        min = fmax(section == SR_SECTION_TEMP ? -20.0 : 0.0, fmin(95.0, min));
        double max = 100.0;
        if (section == SR_SECTION_TEMP) {
            double hottest = (ok && isfinite(value)) ? value : -INFINITY;
            for (unsigned j = 0; j < hist->count; ++j)
                if (isfinite(hist->data[j])) hottest = fmax(hottest, hist->data[j]);
            if (hottest > 100.0)
                max = fmax(max, ceil((hottest + 5.0) / 10.0) * 10.0);
        }
        static const char *bars[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        unsigned points = opts->points < SR_HISTORY_CAP ? opts->points : SR_HISTORY_CAP;
        unsigned count = hist->count < points ? hist->count : points;
        for (unsigned i = count; i < points; ++i) append(&w, "·");
        for (unsigned i = 0; i < count; ++i) {
            unsigned index = (hist->next + SR_HISTORY_CAP - count + i) % SR_HISTORY_CAP;
            double fraction = (hist->data[index] - min) / (max - min);
            if (!isfinite(fraction)) fraction = 0.0;
            int bar = (int)floor(fmax(0.0, fmin(1.0, fraction)) * 7.0);
            append(&w, "%s", bars[bar]);
        }
        append(&w, " ");
    }
    append(&w, "%s", text);
    if (opts->colors) append(&w, "</span>");
    append(&w, "</span>");
}
