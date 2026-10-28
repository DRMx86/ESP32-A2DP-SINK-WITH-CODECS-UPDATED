# Independent ESP32 Bluetooth host

The project goal is a small, independently authored Bluetooth Classic + BLE host,
with simultaneous operation through the original ESP32's Espressif dual-mode
controller. BTstack is inspected only in `../reference/btstack`.

Current milestone: a portable C11 HCI/ACL/L2CAP channel runtime, basic GAP setup and an isolated
ESP-IDF 5.5.2 probe. This is **not yet an operational Bluetooth host or an audio
firmware replacement**. The staged audio application still uses Bluedroid and
includes the separately requested integer DSP and codec-memory changes.

The GitHub host checkpoint includes the owned host/probe, native tests and their
portable DSP C test dependency. Audio-application/DSP-wrapper/SDK codec changes
described in the local progress report are separate work and are not a completed
application migration in this checkpoint.

| Path | Purpose |
| --- | --- |
| `include/`, `src/` | Independently authored portable core |
| `tests/` | C behavior tests, mocked controller/transport, real concurrent queue test |
| `esp32_probe/` | Controller-only ESP32 build, explicit source dependencies |
| `tools/` | Fixed-path build, tests, size/dependency checks and Linux validation |
| `docs/` | Architecture, upstream layer review, coverage and measurement criteria |
| `_build/` | Ignored, reusable generated outputs; no SDK copies |

From the workspace root:

```powershell
python native_bt/tools/test_host.py
.\native_bt\tools\build_probe.ps1
python native_bt/tools/check_workspace.py
```

The test runner compiles and executes the actual C code, including a 20,000-packet
test using separate producer/consumer threads. Its one current receipt is
`_build/host/results.json`; it includes input hashes. Windows testing uses Clang
without requiring a Visual Studio CRT installation. The target build uses the
existing adjacent SDK and the already installed ESP-IDF toolchain.

The probe performs eight bounded, asynchronous identification commands: Reset,
version, supported commands/features, address, Classic buffer capacity, and LE
features/buffer capacity. `IDENTIFIED` means these responses were validated, not
that pairing, GATT or A2DP works. It then configures event masks, host support,
name/class, SSP enable and legacy advertising parameters/data using the general
command queue. It does not enable advertising or Classic scanning.
The probe is not automatically flashed and uses its own partition layout.

`dbt_runtime` joins the link manager, lazy reassembly, Classic Basic channel
negotiation and gated ACL TX. Tests connect two instances over a simulated
controller transport, transfer fragmented data, and disconnect. They also stall
Classic credits while LE data/control continue. `dbt_gap` has explicit incoming
admission and security-owner hooks; no bonding/security/profile implementation is
implied by enabling SSP in the controller.

There are 51 native groups, including the concurrent queue test. Run
`python native_bt/tools/test_stack_linux.py --host HOST --user USER` for Linux
normal/ASan/UBSan checks; the password prompt is session-only. The default probe
does not require the separate LHDC checkout. `build_probe.ps1 -WithLhdc` enables
its optional decoder compilation without adding it to the host loop.

See [release gates](docs/RELEASE_READINESS.md) before publishing a replacement
firmware. This milestone remains an incomplete development implementation.

See [architecture and layer review](docs/architecture.md) for the implementation
order, actual gaps, lazy codec requirements, and performance acceptance criteria.

The latest state is in [current progress](docs/PROGRESS.md), including the LHDC V5,
integer DSP and immediate-NVS requirements. The audio application now has staged
DSP changes authorized separately from the host migration.
