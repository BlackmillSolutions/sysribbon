/* SPDX-License-Identifier: MIT */
#ifndef SYSRIBBON_METRICS_H
#define SYSRIBBON_METRICS_H

#include <stdbool.h>
#include <stdint.h>

#define SYSRIBBON_PATH_MAX 4096

typedef struct {
    bool cpu_ok, ram_ok, temp_ok, network_ok;
    double cpu_pct, ram_pct, ram_used_gib, ram_total_gib, temp_c;
    double download_bps, upload_bps;
} SrSample;

typedef struct {
    bool have_cpu, have_net;
    uint64_t cpu_total, cpu_idle, net_rx, net_tx;
    int64_t net_time_ns, next_sensor_scan_ns, next_iface_scan_ns;
    int64_t last_temp_read_ns; /* Temperature sensors rarely benefit from 10 Hz I/O. */
    double cached_temp_c;
    bool cached_temp_ok;
    char temp_path[SYSRIBBON_PATH_MAX];
    char iface[64];
    /* For integration tests: set before first collection, default /proc and /sys. */
    const char *proc_root;
    const char *sys_root;
} SrSampler;

enum { SR_CPU = 1 << 0, SR_RAM = 1 << 1, SR_TEMP = 1 << 2, SR_NET = 1 << 3, SR_ALL = 15 };

void sr_sampler_init(SrSampler *sampler);
void sr_sample_selected(SrSampler *sampler, SrSample *sample, unsigned flags);
void sr_sample(SrSampler *sampler, SrSample *sample);

#endif
