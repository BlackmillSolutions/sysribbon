/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../src/sampler.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void dir_create(const char *path) { assert(mkdir(path, 0700) == 0); }
static void file_write(const char *path, const char *s) {
    FILE *fp = fopen(path, "w");
    assert(fp);
    assert(fputs(s, fp) >= 0);
    assert(fclose(fp) == 0);
}

int main(void) {
    char root[] = "/tmp/ml-fixture-XXXXXX", path[4096], proc[256], sys[256];
    assert(mkdtemp(root));
    snprintf(proc, sizeof(proc), "%s/proc", root);
    snprintf(sys, sizeof(sys), "%s/sys", root);
    dir_create(proc); dir_create(sys);

    snprintf(path, sizeof(path), "%s/net", proc); dir_create(path);
    snprintf(path, sizeof(path), "%s/class", sys); dir_create(path);
    snprintf(path, sizeof(path), "%s/class/hwmon", sys); dir_create(path);
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon0", sys); dir_create(path);
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon1", sys); dir_create(path);

    snprintf(path, sizeof(path), "%s/stat", proc);
    file_write(path, "cpu 10 0 10 80 0 0 0 0\n");
    snprintf(path, sizeof(path), "%s/meminfo", proc);
    file_write(path, "MemTotal: 8388608 kB\nMemFree: 100 kB\nMemAvailable: 4194304 kB\n");
    snprintf(path, sizeof(path), "%s/net/route", proc);
    file_write(path, "Iface Destination Gateway Flags\neth0 00000000 0101A8C0 0003\n");
    snprintf(path, sizeof(path), "%s/net/dev", proc);
    file_write(path, "Inter-| Receive | Transmit\n face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed\neth0: 1024 0 0 0 0 0 0 0 512 0 0 0 0 0 0 0\n");
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon0/name", sys);
    file_write(path, "amdgpu\n");
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon0/temp1_input", sys);
    file_write(path, "78000\n");
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon1/name", sys);
    file_write(path, "k10temp\n");
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon1/temp1_label", sys);
    file_write(path, "Tdie\n");
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon1/temp1_input", sys);
    file_write(path, "58000\n");

    SrSampler sampler;
    sr_sampler_init(&sampler);
    sampler.proc_root = proc;
    sampler.sys_root = sys;
    SrSample first, second;
    sr_sample(&sampler, &first);
    assert(!first.cpu_ok && !first.network_ok);
    assert(first.ram_ok && fabs(first.ram_pct - 50.0) < 0.0001);
    assert(first.temp_ok && fabs(first.temp_c - 58.0) < 0.0001);

    snprintf(path, sizeof(path), "%s/stat", proc);
    file_write(path, "cpu 35 0 35 130 0 0 0 0\n");
    snprintf(path, sizeof(path), "%s/net/dev", proc);
    file_write(path, "eth0: 2048 0 0 0 0 0 0 0 2048 0 0 0 0 0 0 0\n");
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 150000000L };
    nanosleep(&ts, NULL);
    sr_sample(&sampler, &second);
    assert(second.cpu_ok && fabs(second.cpu_pct - 50.0) < 0.0001);
    assert(second.network_ok && second.download_bps > 0 && second.upload_bps > 0);
    assert(second.ram_ok && second.temp_ok);

    /* Temperature caching at fast refresh rates (250 ms hwmon limit). */
    snprintf(path, sizeof(path), "%s/class/hwmon/hwmon1/temp1_input", sys);
    file_write(path, "71000\n");
    SrSample cached, refreshed;
    sr_sample_selected(&sampler, &cached, SR_TEMP);
    assert(cached.temp_ok && fabs(cached.temp_c - 58.0) < 0.0001);
    struct timespec pause_temp = { .tv_sec = 0, .tv_nsec = 270000000L };
    nanosleep(&pause_temp, NULL);
    sr_sample_selected(&sampler, &refreshed, SR_TEMP);
    assert(refreshed.temp_ok && fabs(refreshed.temp_c - 71.0) < 0.0001);

    /* Clean up confined temporary fixture paths. */
    const char *files[] = {
        "proc/stat", "proc/meminfo", "proc/net/route", "proc/net/dev",
        "sys/class/hwmon/hwmon0/name", "sys/class/hwmon/hwmon0/temp1_input",
        "sys/class/hwmon/hwmon1/name", "sys/class/hwmon/hwmon1/temp1_input",
        "sys/class/hwmon/hwmon1/temp1_label"
    };
    for (unsigned i = 0; i < sizeof(files)/sizeof(files[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", root, files[i]);
        assert(unlink(path) == 0);
    }
    const char *dirs[] = {"proc/net", "proc", "sys/class/hwmon/hwmon0", "sys/class/hwmon/hwmon1", "sys/class/hwmon", "sys/class", "sys"};
    for (unsigned i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        assert(rmdir(path) == 0);
    }
    assert(rmdir(root) == 0);
    puts("OK fixture: CPU RAM temp selection net counters");
    return 0;
}
