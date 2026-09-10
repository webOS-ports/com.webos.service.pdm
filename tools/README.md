# pdm test harness

Three layers, each catching a different class of problem.

| | what it runs | what it catches |
|---|---|---|
| `tools/build-arches.sh` | cross-compiles for armv7, aarch64 and x86-64 | portability and any compiler warning |
| `tools/run-tests.sh` | the gtest suite, on the target | wrong answers from the logic the daemon computes |
| `tools/luna-smoke.sh` | the live luna API, on the target | crashes and restarts under hostile input |

All three build **this working tree**, committed or not. They pass an
`EXTERNALSRC` fragment to bitbake with `-R`, so the recipe's `SRCREV` is
ignored and nothing in `meta-webos-ports` is modified.

Set `PDM_BUILD_DIR` if your OE build is not at
`/media/herrie/LuneOS/wrynose/webos-ports`. The rest of the defaults live in
`tools/pdm-test-env.sh`.

## Architectures

One machine per architecture, because the differences are real: `uint64_t` is
`unsigned long` on aarch64 and `unsigned long long` on armv7, `long` and
`size_t` are 32-bit on armv7, and only aarch64 carries
`-mbranch-protection=standard`. A clean build on one proves little about the
others — the format-string bugs this suite now guards against only ever fired
on 64-bit.

| arch | MACHINE | runtime target |
|---|---|---|
| armv7 | `mindphone` | none attached — build coverage only |
| aarch64 | `sargo` | Pixel 3a over `adb` |
| x86-64 | `qemux86-64` | LuneOS VirtualBox VM over ssh on port 5522 |

## Build gate

```sh
tools/build-arches.sh                 # all three
tools/build-arches.sh armv7 aarch64   # a subset
```

Exits non-zero if any architecture fails to build **or** emits a single
warning from pdm's own sources. Warnings from the sysroot's headers are
ignored — they are not ours to fix. Per-arch `log.do_compile` copies are left
in the log directory it prints.

## Unit tests

```sh
tools/run-tests.sh sargo
tools/run-tests.sh vbox
tools/run-tests.sh sargo --gtest_filter='PdmUtilsRunCommand.*'
```

Builds the recipe, installs the `com.webos.service.pdm-tests` ipk on the
target and runs `pdm_test` there. Cross-built and run on the real hardware, so
armv7's 32-bit `long` and aarch64's alignment rules are exercised by the same
toolchain the daemon ships with, not by a host build that happens to pass.

Anything after the target is passed through to gtest.

For `vbox`, start the VM first:

```sh
VBoxManage startvm LuneOS-qemux86-64-testing --type headless
```

The suite (`tests/`) covers:

- `PdmUtils::runCommand` — exit status rather than a wait status, 127 for a
  missing binary, 128+signal for a killed child, and that shell
  metacharacters, globs and `$VAR` in an argument are **not** interpreted.
  That last one is the regression test for the `setVolumeLabel`/`format`
  injection; it needs no disk to prove.
- `PdmUtils::toInt` — returns a default rather than throwing, for every string
  shape that makes `std::stoi` throw.
- `DiskFormat::formatCommand`, `PdmFsck::fsckCommand`,
  `PdmFs::volumeLabelCommand` — a caller-supplied label stays in exactly one
  argv entry, options arrive already split, unsupported filesystems produce no
  command at all.
- `getDeviceAction` — unmapped udev actions (`unbind`, `move`, `online`) are
  not arrivals, and looking one up does not grow the table.
- `getDeviceWithName` — a partition name finds its parent device, and the
  prefix match that makes that work does not become a partition match.

## Luna smoke test

```sh
tools/luna-smoke.sh sargo
tools/luna-smoke.sh vbox
```

Talks to the pdm already running on the target: valid calls, schema
violations, drive names that match nothing, malformed payloads, hostile volume
labels, and a subscription. After each round it re-reads
`pidof physical-device-manager` — a changed pid means the daemon died and
systemd restarted it, which is the failure this is looking for.

Read-only and safe to run against a device with a USB stick attached:
`format` is never called, and every drive name used is one no device can
match. What it therefore does *not* cover is the mount/format/fsck path
end-to-end, which needs real removable media; the argv those paths build is
covered by the unit tests instead.

## Known API quirk the smoke test works around

`getAttachedStorageDeviceList`, `getAttachedNonStorageDeviceList` and
`getAttachedDeviceStatus` all end with

```c++
payload.put("returnValue", subscribed);
```

so `returnValue` reports whether the caller was *subscribed*, not whether the
call *succeeded*. A plain `{"subscribe":false}` query returns the right list
alongside `"returnValue":false`, and no `subscribed` field is reported at all.
By the webOS convention that is backwards — `returnValue` is the success flag
and subscription state belongs in `subscribed`.

It is inherited from upstream webosose and changing it changes the API, so it
is left alone here. `tools/luna-smoke.sh` therefore asserts on the list key in
the payload rather than on `returnValue` for those three methods. If you decide
to fix it, the change is `returnValue: true` plus a separate `subscribed`
field, in `PdmLunaService.cpp` at the three `put("returnValue", subscribed)`
sites — and this workaround in the smoke test can go.
