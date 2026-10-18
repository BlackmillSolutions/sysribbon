<div align="center">
  <img src="assets/sysribbon-logo.png" alt="SysRibbon" width="460" />

  # SysRibbon

  **A lightweight, customizable system monitor for the Xfce panel.**

  Linux · Xfce · C11 · GTK 3 · MIT<br>
  **v0.5.0 — development preview**
</div>

SysRibbon shows live CPU, RAM, temperature and network statistics directly in your Xfce panel. It's a native C plugin — not a btop wrapper — and does not spawn a subprocess for each refresh.

```text
CPU ▁▂▃▅▇ 24% │ RAM ▁▃▄▅▆ 6.4/16 GiB │ TEMP ▁▂▃▄▅ 48°C │ ↓1.2 MiB/s ↑60 KiB/s
```

Or activate **minimal mode**:

```text
24% │ 6.4/16 GiB │ 48°C │ ↓1.2 MiB/s ↑60 KiB/s
```

> **Status:** Development preview. The non-GUI tests pass; full UI integration and Debian binary builds have not been validated on every target distro. Report bugs with distro, Xfce and panel versions.

## Features

- Live **CPU usage, memory, temperature and network throughput**
- Optional history sparklines, or snapshot-only display
- Minimal numeric-only mode
- Dynamic green/yellow/red thresholds (or no coloring)
- Stable section widths, resizable via dragging section boundaries
- Separate widths for regular and minimal mode
- Configurable graph floors (CPU/RAM percentages; temperature °C)
- Update interval from **100 ms to 15 s** (not hard real-time)
- Native `/proc` and `/sys` reads; no Python, btop or monitoring daemon
- Per-plugin-instance settings saved using Xfce's configuration files

**Default graph range:** CPU/RAM 0–100%; temperature 30–100 °C, with an automatically increasing upper bound when necessary. RAM graphs scale to each machine's total RAM; they do not assume a fixed amount of memory.

## Requirements

Linux with **Xfce Panel / libxfce4panel-2.0** and GTK 3. This is *not* a GNOME or KDE extension.

## Install on Ubuntu / Xubuntu / Debian

### Option A: Build an APT-managed `.deb` (recommended for testing)

Install the build dependencies:

```sh
sudo apt update
sudo apt install build-essential pkg-config debhelper devscripts dpkg-dev fakeroot \
  libgtk-3-dev libxfce4ui-2-dev libxfce4panel-2.0-dev libxfce4util-dev
```

Clone this repository or unpack the source archive and enter its root directory:

```sh
git clone https://github.com/BlackmillSolutions/sysribbon.git
cd sysribbon
./tools/build-deb.sh
```

Install the resulting file from the **parent directory** (the architecture portion may differ):

```sh
sudo apt install ../xfce4-sysribbon-plugin_0.5.0-1_*.deb
xfce4-panel -r
```

This registers the package with APT, so removal is later simply:

```sh
sudo apt remove xfce4-sysribbon-plugin
```

> **Local `.deb` ≠ official repository listing.** You can install a local `.deb` through `apt`, but `sudo apt install xfce4-sysribbon-plugin` **without a file path** works only after a repository that APT knows about actually publishes the package. See [PACKAGING.md](PACKAGING.md).

### Option B: Build and install from source

Install the GTK/Xfce development dependencies (listed above), then:

```sh
make test
make
sudo make install
xfce4-panel -r
```

To uninstall this *manually installed* copy from the same directory:

```sh
sudo make uninstall
xfce4-panel -r
```

**Avoid mixing** an unmanaged `sudo make install` copy with the `.deb` package. Prefer APT for future updates.

### Add to the panel

Right-click an Xfce panel → **Panel → Add New Items** → **SysRibbon**. Right-click SysRibbon → **Properties** to configure it.

## Migrating from MetricsLine v0.4

This release **fully changes the Xfce module ID** from `metricsline` to `sysribbon`, along with its library and desktop metadata. It is not an in-place rename of existing panel instances. **Remove the old MetricsLine panel item and manually uninstall its files before installing SysRibbon.** Your old per-instance settings aren't migrated automatically.

See [MIGRATION.md](MIGRATION.md) for exact commands and the optional configuration backup.

## Configuration

| Settings area | What you can change |
| --- | --- |
| Display | Visible metrics, update interval, history length, colors, sparklines, minimal mode |
| Widths & thresholds | Section widths (normal/minimal), warning and critical thresholds |
| Scaling | Lower bounds for CPU, RAM and temperature history graphs |

Drag each section's **right edge** to resize it; the resize cursor should appear only near its edge. Values are not clickable and **do not launch btop**.

## Design and performance

| Metric | Data source |
| --- | --- |
| CPU | `/proc/stat` delta counters |
| RAM | `/proc/meminfo` |
| Network | `/proc/net/route`, `/proc/net/dev` |
| Temperature | `/sys/class/hwmon`, `/sys/class/thermal` |

A GLib timer drives updates. History is bounded in memory. Disabled metrics are skipped, and temperature is sampled no more often than approximately every 250 ms. A 100-ms timer is best-effort and will be affected by system load.

## Known limitations

- The default IPv4 network interface is used; IPv6-only and complex routing setups are not yet comprehensively supported.
- Temperature reporting requires kernel-exposed sensors.
- The first CPU/network sample establishes a baseline.
- Some Xfce versions and vertical panel configurations need more hands-on UI testing.
- Debian packaging is provided for local builds and future upstream review; **the package has not been accepted into Debian or Ubuntu repositories**.

## Maintainers and contributors

Pull requests, bug reports and reproducible test cases welcome. For panel issues, include your Linux distribution, Xfce version, panel orientation and steps to reproduce.

**Packaging/release maintainers:** Before publishing packages, replace the template name, email and GitHub URL in `debian/control`, `debian/changelog`, `debian/copyright` and this README. See [PACKAGING.md](PACKAGING.md).

## License

MIT — see [LICENSE](LICENSE).
