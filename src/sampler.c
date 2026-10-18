/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "sampler.h"

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GIB (1024.0 * 1024.0 * 1024.0)

static int64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void path_join(char *out, size_t cap, const char *root, const char *rel) {
    if (snprintf(out, cap, "%s/%s", root, rel) >= (int)cap)
        out[0] = '\0';
}

static bool first_line(const char *path, char *out, size_t cap) {
    if (!*path) return false;
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    bool ok = fgets(out, (int)cap, fp) != NULL;
    fclose(fp);
    if (ok) out[strcspn(out, "\r\n")] = '\0';
    return ok;
}

static bool temp_from_file(const char *path, double *out) {
    char buf[128], *end;
    if (!first_line(path, buf, sizeof(buf))) return false;
    double val = strtod(buf, &end) / 1000.0;
    if (end == buf || !isfinite(val) || val < -20 || val > 125)
        return false;
    *out = val;
    return true;
}

static void collect_cpu(SrSampler *s, SrSample *out) {
    char path[SYSRIBBON_PATH_MAX], line[512];
    path_join(path, sizeof(path), s->proc_root, "stat");
    if (!first_line(path, line, sizeof(line))) return;
    unsigned long long f[8];
    if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7]) != 8)
        return;
    uint64_t total = 0;
    for (int i = 0; i < 8; ++i) total += (uint64_t)f[i];
    uint64_t idle = (uint64_t)f[3] + (uint64_t)f[4];
    if (s->have_cpu && total >= s->cpu_total && idle >= s->cpu_idle) {
        uint64_t dt = total - s->cpu_total, di = idle - s->cpu_idle;
        if (dt && di <= dt) {
            out->cpu_ok = true;
            out->cpu_pct = fmax(0, fmin(100, 100.0 * (double)(dt - di) / dt));
        }
    }
    s->cpu_total = total;
    s->cpu_idle = idle;
    s->have_cpu = true;
}

static void collect_ram(SrSampler *s, SrSample *out) {
    char path[SYSRIBBON_PATH_MAX], line[256];
    path_join(path, sizeof(path), s->proc_root, "meminfo");
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    unsigned long long total = 0, available = 0, free_kib = 0;
    bool has_available = false;
    while (fgets(line, sizeof(line), fp)) {
        unsigned long long n;
        if (sscanf(line, "MemTotal: %llu kB", &n) == 1) total = n;
        else if (sscanf(line, "MemAvailable: %llu kB", &n) == 1) {
            available = n;
            has_available = true;
        } else if (sscanf(line, "MemFree: %llu kB", &n) == 1) free_kib = n;
    }
    fclose(fp);
    if (!has_available) available = free_kib; /* conservative fallback on old kernels */
    if (total == 0 || available > total) return;
    out->ram_ok = true;
    out->ram_pct = 100.0 * (double)(total - available) / total;
    out->ram_total_gib = (double)total * 1024.0 / GIB;
    out->ram_used_gib = (double)(total - available) * 1024.0 / GIB;
}

static int temp_label_score(const char *chip, const char *label) {
    int base = 0;
    if (!strcmp(chip, "k10temp") || !strcmp(chip, "zenpower")) base = 60;
    else if (!strcmp(chip, "coretemp")) base = 50;
    else if (!strcmp(chip, "cpu_thermal") || !strcmp(chip, "soc_thermal")) base = 30;
    if (!base) return -1; /* never confuse GPU, SSD or motherboard sensors with CPU */
    if (strstr(label, "Tdie")) return base + 25;
    if (strstr(label, "Package") || strstr(label, "package")) return base + 22;
    if (strstr(label, "Tctl")) return base + 15;
    if (strstr(label, "Core")) return base + 10;
    return base;
}

static void sensor_search(SrSampler *s) {
    char hwroot[SYSRIBBON_PATH_MAX];
    path_join(hwroot, sizeof(hwroot), s->sys_root, "class/hwmon");
    DIR *root = opendir(hwroot);
    int best_score = -1;
    s->temp_path[0] = '\0';
    if (root) {
        struct dirent *entry;
        while ((entry = readdir(root))) {
            if (strncmp(entry->d_name, "hwmon", 5)) continue;
            char dir[SYSRIBBON_PATH_MAX], path[SYSRIBBON_PATH_MAX], chip[128];
            if (snprintf(dir, sizeof(dir), "%s/%s", hwroot, entry->d_name) >= (int)sizeof(dir)) continue;
            path_join(path, sizeof(path), dir, "name");
            if (!first_line(path, chip, sizeof(chip))) continue;
            DIR *children = opendir(dir);
            if (!children) continue;
            struct dirent *item;
            while ((item = readdir(children))) {
                int n = 0;
                if (sscanf(item->d_name, "temp%d", &n) != 1 || n < 1 || n > 256)
                    continue;
                char expected[32];
                snprintf(expected, sizeof(expected), "temp%d_input", n);
                if (strcmp(item->d_name, expected)) continue;
                if (snprintf(path, sizeof(path), "%s/%s", dir, item->d_name) >= (int)sizeof(path)) continue;
                double dummy;
                if (!temp_from_file(path, &dummy)) continue;
                char label_path[SYSRIBBON_PATH_MAX], label[128] = "";
                if (snprintf(label_path, sizeof(label_path), "%s/temp%d_label", dir, n) >= (int)sizeof(label_path)) continue;
                first_line(label_path, label, sizeof(label));
                int score = temp_label_score(chip, label);
                if (score > best_score) {
                    best_score = score;
                    snprintf(s->temp_path, sizeof(s->temp_path), "%s", path);
                }
            }
            closedir(children);
        }
        closedir(root);
    }
    if (best_score >= 0) return;
    /* Prefer explicitly CPU-labelled thermal zones; no GPU / ACPI fallback. */
    char tzroot[SYSRIBBON_PATH_MAX];
    path_join(tzroot, sizeof(tzroot), s->sys_root, "class/thermal");
    root = opendir(tzroot);
    if (!root) return;
    struct dirent *entry;
    while ((entry = readdir(root))) {
        if (strncmp(entry->d_name, "thermal_zone", 12)) continue;
        char dir[SYSRIBBON_PATH_MAX], path[SYSRIBBON_PATH_MAX], type[128];
        if (snprintf(dir, sizeof(dir), "%s/%s", tzroot, entry->d_name) >= (int)sizeof(dir)) continue;
        path_join(path, sizeof(path), dir, "type");
        if (!first_line(path, type, sizeof(type))) continue;
        if (!strstr(type, "cpu") && !strstr(type, "CPU") && !strstr(type, "x86_pkg_temp") &&
            !strstr(type, "soc_thermal")) continue;
        path_join(path, sizeof(path), dir, "temp");
        double dummy;
        if (temp_from_file(path, &dummy)) {
            snprintf(s->temp_path, sizeof(s->temp_path), "%s", path);
            break;
        }
    }
    closedir(root);
}

static void collect_temp(SrSampler *s, SrSample *out, int64_t now) {
    /* At 100 ms UI refresh, read hwmon at most once every 250 ms.
     * The UI still updates CPU/RAM/network on every tick. */
    if (s->last_temp_read_ns > 0 && now >= s->last_temp_read_ns &&
        now - s->last_temp_read_ns < 250000000LL) {
        out->temp_ok = s->cached_temp_ok;
        out->temp_c = s->cached_temp_c;
        return;
    }
    s->last_temp_read_ns = now;
    double val;
    if (s->temp_path[0] && temp_from_file(s->temp_path, &val)) {
        out->temp_ok = true;
        out->temp_c = val;
    } else if (now >= s->next_sensor_scan_ns) {
        s->next_sensor_scan_ns = now + 60000000000LL; /* Failed scan: 60 s backoff. */
        sensor_search(s);
        if (s->temp_path[0] && temp_from_file(s->temp_path, &val)) {
            out->temp_ok = true;
            out->temp_c = val;
        }
    }
    s->cached_temp_ok = out->temp_ok;
    s->cached_temp_c = out->temp_c;
}

static void find_interface(SrSampler *s) {
    char path[SYSRIBBON_PATH_MAX], line[256];
    path_join(path, sizeof(path), s->proc_root, "net/route");
    FILE *fp = fopen(path, "r");
    char iface[64] = "";
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char name[64];
            unsigned long destination = 1, gateway = 0, flags = 0;
            if (sscanf(line, "%63s %lx %lx %lx", name, &destination, &gateway, &flags) == 4 &&
                destination == 0 && (flags & 1)) {
                snprintf(iface, sizeof(iface), "%s", name);
                break;
            }
        }
        fclose(fp);
    }
    if (strcmp(s->iface, iface) != 0) {
        snprintf(s->iface, sizeof(s->iface), "%s", iface);
        s->have_net = false;
    }
}

static void collect_net(SrSampler *s, SrSample *out, int64_t now) {
    if (now >= s->next_iface_scan_ns) {
        s->next_iface_scan_ns = now + 30000000000LL;
        find_interface(s);
    }
    if (!s->iface[0]) return;
    char path[SYSRIBBON_PATH_MAX], line[512];
    path_join(path, sizeof(path), s->proc_root, "net/dev");
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    uint64_t rx = 0, tx = 0;
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *start = line;
        while (isspace((unsigned char)*start)) ++start;
        char *end = colon - 1;
        while (end >= start && isspace((unsigned char)*end)) *end-- = '\0';
        if (strcmp(start, s->iface)) continue;
        unsigned long long numbers[16];
        int parsed = sscanf(colon + 1,
            "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &numbers[0], &numbers[1], &numbers[2], &numbers[3],
            &numbers[4], &numbers[5], &numbers[6], &numbers[7],
            &numbers[8], &numbers[9], &numbers[10], &numbers[11],
            &numbers[12], &numbers[13], &numbers[14], &numbers[15]);
        if (parsed == 16) {
            rx = (uint64_t)numbers[0];
            tx = (uint64_t)numbers[8];
            found = true;
        }
        break;
    }
    fclose(fp);
    if (!found) { s->have_net = false; s->next_iface_scan_ns = 0; return; }
    if (s->have_net && rx >= s->net_rx && tx >= s->net_tx && now > s->net_time_ns) {
        double seconds = (double)(now - s->net_time_ns) / 1e9;
        if (seconds >= 0.05 && seconds < 120) {
            out->network_ok = true;
            out->download_bps = (double)(rx - s->net_rx) / seconds;
            out->upload_bps = (double)(tx - s->net_tx) / seconds;
        }
    }
    s->net_rx = rx;
    s->net_tx = tx;
    s->net_time_ns = now;
    s->have_net = true;
}

void sr_sampler_init(SrSampler *sampler) {
    memset(sampler, 0, sizeof(*sampler));
    sampler->proc_root = "/proc";
    sampler->sys_root = "/sys";
}

void sr_sample_selected(SrSampler *sampler, SrSample *sample, unsigned flags) {
    memset(sample, 0, sizeof(*sample));
    int64_t now = monotonic_ns();
    if (flags & SR_CPU) collect_cpu(sampler, sample);
    if (flags & SR_RAM) collect_ram(sampler, sample);
    if (flags & SR_TEMP) collect_temp(sampler, sample, now);
    if (flags & SR_NET) collect_net(sampler, sample, now);
}

void sr_sample(SrSampler *sampler, SrSample *sample) {
    sr_sample_selected(sampler, sample, SR_ALL);
}
