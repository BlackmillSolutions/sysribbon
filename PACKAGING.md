# SysRibbon: Debian / Ubuntu packaging guide

SysRibbon ships a Debian-style source packaging skeleton in `debian/`. **It does not automatically appear in Ubuntu's official repositories by uploading an archive to GitHub.**

## 0. Package naming

- Upstream project/source package: `sysribbon`
- Binary package name: **`xfce4-sysribbon-plugin`**
- Xfce module: `sysribbon`
- Library: `libsysribbon.so`
- Xfce metadata: `sysribbon.desktop`
- Version in this preview: `0.5.0-1` (upstream `0.5.0`, packaging revision `1`)

## 1. Prepare before public release

Verify the package identity before each release:

- `debian/control`: `Maintainer: Ali Karadag <blackmillsolutions@gmail.com>`.
- `debian/changelog`: use the same maintainer identity and keep the release date current. `dch` (from `devscripts`) is convenient.
- `debian/copyright`: upstream source is `https://github.com/BlackmillSolutions/sysribbon`.
- `README.md`: the clone example uses `https://github.com/BlackmillSolutions/sysribbon.git`.

Also verify upstream authorship/copyright, the `LICENSE` text, and generated logo usage. Review the current Debian Policy and adjust `Standards-Version` after assessing policy changes. For an official upload you must become an active maintainer and handle security reports and bug fixes.

## 2. Build on Debian/Ubuntu

```sh
sudo apt update
sudo apt install build-essential pkg-config devscripts debhelper dpkg-dev fakeroot lintian \
  libgtk-3-dev libxfce4ui-2-dev libxfce4panel-2.0-dev libxfce4util-dev
```

From the repository root:

```sh
./tools/build-deb.sh
```

This runs `dpkg-buildpackage -b -us -uc` and generates an **unsigned binary package** in the parent folder. Install/test locally with:

```sh
sudo apt install ../xfce4-sysribbon-plugin_0.5.0-1_*.deb
xfce4-panel -r
```

Check contents/dependencies first:

```sh
dpkg-deb --contents ../xfce4-sysribbon-plugin_*.deb
dpkg-deb --info ../xfce4-sysribbon-plugin_*.deb
lintian --profile debian ../sysribbon_0.5.0-1_amd64.changes
```

The Lintian example uses version `0.5.0-1` and architecture `amd64`; adjust those
parts to match your build. The `.changes` and `.buildinfo` filenames use the
**source package name** `sysribbon`, while the installable `.deb` uses the
**binary package name** `xfce4-sysribbon-plugin`. The optional
`xfce4-sysribbon-plugin-dbgsym_*.ddeb` contains debugging symbols. These different
names are expected; do not rename the artifacts. The Debian Lintian profile also
lets you check this Debian-targeted package when building on Ubuntu.

Expected installed paths (on amd64):

```text
/usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libsysribbon.so
/usr/share/xfce4/panel/plugins/sysribbon.desktop
/usr/share/icons/hicolor/128x128/apps/sysribbon.png
```

The `dh` packaging system computes shared-library dependencies (`${shlibs:Depends}`) and handles uninstall/upgrade ownership using `dpkg`. On another architecture the multiarch library path differs.

For stronger release testing, build in a clean chroot using `sbuild` or `pbuilder`, and run `lintian` and manual Xfce UI tests on supported Debian/Ubuntu releases.

## 3. Generate Debian source package for review

```sh
./tools/build-source.sh
```

The script makes an upstream `sysribbon_0.5.0.orig.tar.gz` (excluding `debian/`) and calls `dpkg-buildpackage -S -us -uc`. The parent folder will contain a `.dsc`, `.debian.tar.xz`, `.changes`, and the original upstream `.tar.gz`.

**For an official upload**, build signed source packages using your OpenPGP identity, e.g. `dpkg-buildpackage -S` once maintainer identity and signing setup are correct. A reviewer/sponsor will verify archive quality, license and policy compliance; unsigned test artifacts are not official submissions.

## 4. Make it available via plain `apt install` from official Ubuntu repositories

### Preferred route: Debian → Ubuntu

1. Create a public upstream GitHub repository with tagged releases and working build/tests. Check for existing package names.
2. Open a Debian *Intent to Package* (ITP) request/bug and coordinate with Debian's Xfce packaging team if they are interested. Don't assume acceptance.
3. Prepare policy-compliant source packaging, check clean builds, lintian output, licensing, ABI and Xfce integration.
4. Publish your source package on [Debian Mentors](https://mentors.debian.net/) or propose it via Salsa, and **find a Debian Developer sponsor** for review and upload. Debian maintainers without upload rights cannot upload to the official archive themselves.
5. Once accepted into Debian `unstable`, it can usually be **automatically synced into Ubuntu** before the Debian Import Freeze. After that, a sync request and/or Ubuntu sponsorship may be needed. Acceptance into Debian doesn't guarantee inclusion in an already-released Ubuntu version.
6. Once Ubuntu has published the package in a repository enabled for the user's release, installation becomes:

   ```sh
   sudo apt update
   sudo apt install xfce4-sysribbon-plugin
   ```

### Alternative: directly into Ubuntu

Submit a [Launchpad new-package request](https://ubuntu.com/project/docs/how-ubuntu-is-made/processes/new-packages/) tagged `needs-packaging`. For a package not arriving through Debian, find an Ubuntu uploader/sponsor and pass the archive review. This is a review/maintainership process, not a self-service upload to an "APT marketplace".

### Alternative for earlier availability: PPA / own APT repository

You can publish signed binary packages via a private APT repository, or set up a Launchpad PPA for Ubuntu. Users would add that repository/PPA **first**, then run `sudo apt install xfce4-sysribbon-plugin`. That is **not the same** as inclusion in Ubuntu's official repositories.

## 5. Updates and uninstallation after using APT

```sh
sudo apt update
sudo apt upgrade
sudo apt remove xfce4-sysribbon-plugin
```

`apt upgrade` only finds updated versions when an enabled repository serves them. For purging package-managed configuration files, `sudo apt purge xfce4-sysribbon-plugin` is available; per-user Xfce settings are not necessarily removed by purge.

**Never** run `sudo make uninstall` against an APT-managed installation — use `apt remove` instead.

## 6. Release checklist

- [x] All maintainer and repository placeholders replaced
- [ ] Copyright and logo rights verified; LICENSE and Debian DEP-5 metadata reviewed
- [ ] Correct Xfce API compatibility tested on target systems
- [ ] `make test` passes; Debian package builds in a clean environment
- [ ] Manual desktop testing for hover, drag-to-resize, minimal mode, 100 ms interval
- [ ] `lintian` reviewed; runtime shlibs correctly calculated
- [ ] Package removal and upgrade tested
- [ ] Signed release and source package prepared for sponsor review

Official resources:
- [Debian Guide for Maintainers](https://www.debian.org/doc/manuals/debmake-doc/)
- [Debian Developers Reference — Sponsoring](https://www.debian.org/doc/manuals/developers-reference/developers-reference.html#sponsoring-packages)
- [Ubuntu — New packages](https://ubuntu.com/project/docs/how-ubuntu-is-made/processes/new-packages/)
