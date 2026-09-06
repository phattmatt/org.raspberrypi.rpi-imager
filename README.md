# Raspberry Pi Imager Flatpak

This manifest packages **Raspberry Pi Imager v2.0.11.1** as
`org.raspberrypi.rpi-imager`, using the KDE 6.9 runtime. The upstream release
and all seven external libraries are pinned by tag and commit.

## Build and run

Install Flatpak and flatpak-builder, add the Flathub remote, then run:

```sh
flatpak install --user flathub org.kde.Platform//6.9 org.kde.Sdk//6.9
flatpak-builder --user --force-clean --repo=repo build org.raspberrypi.rpi-imager.yaml
flatpak-builder --run build org.raspberrypi.rpi-imager.yaml rpi-imager
```

To install the locally built application:

```sh
flatpak-builder --user --install --force-clean build org.raspberrypi.rpi-imager.yaml
```

To install a provided bundle, use the same per-user scope as the runtime:

```sh
flatpak install --user ./build/org.raspberrypi.rpi-imager-2.0.11.1-x86_64.flatpak
flatpak run org.raspberrypi.rpi-imager
```

Downloads happen before the build sandbox starts. All FetchContent sources
are supplied by the manifest; timezone and wireless-country data use the
snapshots included in the upstream source. No build-time network access is
needed. To verify this after downloading sources:

```sh
flatpak-builder --user --download-only build org.raspberrypi.rpi-imager.yaml
flatpak-builder --user --force-clean --disable-download build org.raspberrypi.rpi-imager.yaml
```

## Changes required for 2.x

The old manifest targets 1.9.6. Its large `remove-vendoring.patch` no longer
matches upstream's split CMake files. This package uses upstream's bundled
library build, with explicit offline sources including the newly required
curl and libusb. Unused dependency submodules are disabled. Upstream now
patches libarchive's static zstd detection itself.

Two small build patches give nghttp2's generated `config.h` priority over
Imager's header and preserve the exact release version despite downstream
patches. Explicit `-fPIC` avoids Qt data copy relocations associated with
startup crashes in the earlier 2.x packaging attempt.

`fix-offline-timezones.patch` explicitly embeds upstream's `timezones.txt` at
`:/timezones.txt`. With online generation disabled, upstream otherwise omits
this resource, leaving the time-zone selector empty. The country list is
already embedded by upstream.

Upstream's Linux application expects root and uses direct device opens and
unmount system calls. Flatpak cannot elevate through `pkexec`; `--device=all`
alone does not grant permission to open host block devices. The Flatpak
patch and `flatpak-udisks2.h` instead use the host's UDisks2 service to:

- Unmount the selected disk and its partitions in the host mount namespace.
- Request an authorized read/write descriptor with exclusive access.
- Keep that descriptor and lock when toggling direct I/O.
- Stop on denied authorization, cancellation, or busy filesystems.

The GUI runs as the normal user. The host must provide UDisks2 (2.7.3 or
newer) and a working polkit authentication agent. Host policy still decides
which devices the user can access. No host helper or polkit policy is installed.
USB boot/fastboot access separately depends on the host's USB device
permissions; udev rules inside a Flatpak cannot change those permissions.

Upstream's `com.raspberrypi` desktop file is renamed to the existing
`org.raspberrypi` Flatpak ID. The launcher retains `%u` and the Pi Connect
URI association; the session bus permission allows callbacks to reach the
running instance. The application does not generate a host launcher pointing
at its sandbox-only `/app/bin` path. Release metadata remains maintained here.

## Validation

After building, check the packaged time-zone resource and mock UDisks2 behavior:

```sh
./tests/run.sh
```

The resource check runs the actual executable with a test-only replacement
for its main function, after Qt has registered its embedded resources. It
requires a populated list containing Europe/London, America/New_York, and
Asia/Tokyo. Run just this check with `./tests/resources.sh`.

The host needs `dbus-run-session`, Python 3, `python3-dbus`, and
`python3-gi`. The C++ checks compile against the Flatpak SDK. They use a
private mock bus and temporary files, never real storage devices. Coverage
includes D-Bus object decoding, device/partition matching, unmount failures,
authorization cancellation, descriptor ownership, and close-on-exec.

To check the exact version and exercise raw, gzip, xz, and zstd image writing
with verification into temporary regular files:

```sh
flatpak-builder --run build org.raspberrypi.rpi-imager.yaml python3 tests/smoke.py
```

Before publishing, also test with a disposable SD card: enumerate it, cancel
an authorization request, write and verify an image with a mounted partition,
and repeat a write. Check both X11 and Wayland startup and a Pi Connect
browser callback. Mock tests cannot verify host polkit policy or physical I/O.

Local validation completed on x86_64: offline build; exact-version startup
against KDE Platform; headless GUI startup and OS catalogue retrieval over
HTTP/2; host UDisks2 enumeration; mock authorization tests; raw/gzip/xz/zstd
write and verification; packaged time-zone resource check (432 zones); desktop and AppStream validation. Physical SD-card
I/O, visible X11/Wayland sessions, browser sign-in, and aarch64 remain untested.

References: [upstream release](https://github.com/raspberrypi/rpi-imager/releases/tag/v2.0.11.1),
[earlier Flathub 2.x attempt](https://github.com/flathub/org.raspberrypi.rpi-imager/pull/65),
[UDisks2 block-device API](https://storaged.org/doc/udisks2-api/latest/gdbus-org.freedesktop.UDisks2.Block.html).
