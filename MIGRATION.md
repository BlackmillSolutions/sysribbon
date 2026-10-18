# Migrating from MetricsLine to SysRibbon

SysRibbon v0.5.0 **renames the real plugin module**: `metricsline` to `sysribbon`.
This is not an in-place upgrade: Xfce recognizes a new plugin ID. Follow these steps.

1. Optional: back up your Xfce panel settings and plugin RC files before changing the panel:

   ```sh
   mkdir -p "$HOME/xfce-panel-backup"
   cp -a "$HOME/.config/xfce4/panel" "$HOME/xfce-panel-backup/" 2>/dev/null || true
   cp -a "$HOME/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml" "$HOME/xfce-panel-backup/" 2>/dev/null || true
   ```
2. Right-click the old **MetricsLine** item → **Remove**, before deleting its library.
3. If the old plugin was installed with `sudo make install`, go to the **old** v0.4 source directory and run:

   ```sh
   sudo make uninstall
   ```

   This removes only the paths listed in the old version's Makefile. Alternatively, inspect `make -n uninstall` before running it. In the v0.4 source the old module and descriptor paths are normally:
   `/usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)/xfce4/panel/plugins/libmetricsline.so`
   and `/usr/share/xfce4/panel/plugins/metricsline.desktop`.
   Its uninstall target can also remove the old `sysribbon.png` icon; the new package installs the icon again.
4. Install SysRibbon as a Debian package (recommended) or from source.
5. Restart the panel with `xfce4-panel -r`, then **Panel → Add New Items → SysRibbon**.
6. Reconfigure the newly added instance. Old per-instance settings remain in backed-up Xfce configuration files but are **not migrated automatically**.

## If you lost the old source folder

First inspect **exactly** which paths exist, then remove only the old manually installed files (never wildcard-remove Xfce plugin directories):

```sh
OLD_LIB="$(pkg-config --variable=libdir libxfce4panel-2.0)/xfce4/panel/plugins/libmetricsline.so"
ls -l "$OLD_LIB" /usr/share/xfce4/panel/plugins/metricsline.desktop
sudo rm -f -- "$OLD_LIB" /usr/share/xfce4/panel/plugins/metricsline.desktop
```

Leave the icon alone in this manual alternative; package installation will take over the icon. If `pkg-config` or any paths differ, do not guess: use `find /usr/lib /usr/local/lib -name libmetricsline.so` to inspect first.

**Do not use** `apt remove metricsline`: previous versions were installed manually and normally weren't managed by apt.
