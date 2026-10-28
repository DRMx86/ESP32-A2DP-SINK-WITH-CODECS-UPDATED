# Current project progress

Updated 2026-10-28. Goal **active and incomplete**.

This report covers the full local working tree. The GitHub host checkpoint is
limited to the owned host/probe, tests, docs and portable DSP test dependency;
it does not publish or replace the locally modified Bluedroid audio application.

## Requirements retained

- Independent Classic + BLE host, concurrent operation and ESP32-supported features.
  BTstack is a pinned reference only: no copying, includes, linkage or runtime use.
- Preserve/optimize SBC, AAC, aptX, aptX-HD, aptX-LL, LDAC, Opus; add **LHDC V5**
  from `D:\ESP32 Bluetooth\LHDC-V5-Decoder-main` without copying its bundled SDK.
- Only active codec/configuration working data and necessary RAM table copies;
  release inactive resources and avoid overlapping full decoder working sets.
- Integer EQ/DSP; fix BLE EQ pops and cross-task filter state mutation.
- **Commit every EQ change to NVS immediately, even during playback.** The user
  explicitly rejected deferred, coalesced or playback-idle persistence.
- User authorized autonomous work, SSH/Linux codec tests and the opened Firefox.
  Credentials remain session-only; none are saved in source or reports.
- Organized sources, fixed reusable build paths; no SDK clones, recordings or
  repeated build snapshots. Size checks: 96 MiB reference / 512 MiB generated.

## Implemented and checked

| Work | Current evidence | Limit |
| --- | --- | --- |
| BTstack | Pinned `51e11ce3692abfe31b4ee3d66cffdeb1ae1626f2`; shallow sparse checkout ~70.6 MiB | Reference only |
| HCI | Owned FIFO, framing, eight-command identification, copied four-slot general command queue, completion/status distinction, credit gates and timeout/abort callbacks | Native/ESP32 builds; target probe result is recorded separately below |
| Runtime/ACL | HCI events drive Classic/LE links, credits, disconnect cleanup and L2CAP dispatch in one owner; live LE ATT round trips passed | Classic RF channel establishment and simultaneous RF scheduling remain unqualified |
| Basic L2CAP | Lazy RX reassembly; incoming/outgoing Classic channels; both configuration directions, MTUs, continuation, option negotiation, disconnect, RTX/ERTX, duplicate handling and information responses | Basic mode only; ERTM/LE credit channels and production RX flow control pending |
| Scheduling | One borrowed outbound SDU per connection, round-robin ACL admission, per-link signaling selection, full-PDU signaling commit and immutable transport buffer lifetime | Simulated independent Classic/LE pools and asynchronous transports; not measured radio fairness |
| GAP | Event masks, LE host support, local name/class, SSP enable, legacy advertising parameters/data/scan response; explicit Classic scan and LE advertising controls | Incoming Classic policy hook; security requests default to negative replies without a security owner. Bonding/privacy/initiator procedures pending |
| ATT/GATT server | Owned LE ATT server and runtime bridge; discovery, reads/blobs/multiple, MTU 517, writes/commands, prepare/execute/cancel, per-client CCCDs, security policy, notify/indicate, deferred worker completion | Product command/OTA binding, real security/bond store, GATT client/EATT/signed writes/robust caching pending |
| Tests | 65 groups pass on Windows/Linux, including 10,240 generated ATT inputs, paired bearer tests and 20,000 concurrent FIFO packets; Linux ASan/UBSan passes | Host behavior and memory safety; separate limited RF evidence below |
| Integer DSP | Flash coefficient tables for 13 rates; Q4.28 coefficients/Q5.27 headroom, fractional error carry, 20 ms ramps, render-owned histories, atomic controls | Embedded CPU and audible tests pending |
| DSP numerical tests | 36 cases at 44.1/48/96/192 kHz; max magnitude error ~0.00000145 dB against the coefficient response; native state 1128 bytes | Not ESP32 timing or acoustic proof |
| C++ integration | Actual DSP/storage headers: normal, ASan/UBSan and ThreadSanitizer pass with concurrent control/render/meter requests | NVS is mocked; no physical power-loss test |
| EQ persistence | Immediate single packed tuple write + commit; legacy-key load fallback; write/commit failures tested; latest app builds | Physical flash/power-loss tests pending |
| LHDC optimization | Active-size reference cosine table; release old allocation before resize; release inactive reference cache; retain explicitly forced diagnostic cache | Reference/fallback path only; no normal-fast-path CPU claim |
| LHDC tests | Baseline/candidate reference vectors and fast transforms byte-identical; resize/OOM/retry checks and ASan/UBSan pass | Full encoded-frame, ESP32 timing and playback pending |
| LHDC adapter | Raw-frame lifecycle adapter and portable sibling decoder component compile for ESP32 | Adapter lifecycle tests and A2DP negotiation integration pending |
| AAC | Active-only aligned workspace; actual AAC-LC raw-frame PCM byte-identical at 44.1/48 kHz mono/stereo; address sanitizer, failure/retry and no-overlap reinit checks pass | Target permanent workspace reduced from 20720 bytes to a 4-byte pointer; actual embedded heap/activation tests pending |
| Opus | Lazy fragment storage; scratch sized by channels/frame duration; 14 Android/PipeWire configuration cases with actual CIE parser and system libopus pass byte-identically, including fragmentation and ASan/UBSan | Native backend tests, not ESP32 timing/RF proof; target static context reduced from 6164 to 24 bytes |
| Codec switching | Old decoder cleaned up before allocating a different decoder's scratch/working set; AAC cleanup now releases its underlying workspace | Hardware switch/fragmentation tests pending |
| Audio app | Integer DSP, immediate NVS, AAC and Opus changes integrated; ESP32 app builds and trial flash passed full readback; live idle diagnostics observed | Playback/coexistence unqualified; LHDC not yet advertised by this app |

Reference cosine-table payload: N=240: 30720 -> 3840 bytes; N=480: 30720 -> 7680;
N=960: 30720 -> 15360; N=1920 unchanged at 30720. These are Linux allocation
measurements of the selected path, not total codec RAM or embedded performance.

## Still pending

GAP initiator/discovery procedures and recovery; production RX flow control;
L2CAP modes beyond Basic; Classic security and BLE SMP/bonding/privacy;
product ATT/GATT integration, GATT client/advanced features; SDP;
AVDTP/A2DP/AVRCP; remaining profiles; complete app migration; concurrent Classic/BLE
hardware validation; equivalent-feature memory/CPU/latency comparisons.

AAC and Opus lifetime changes are implemented with native PCM/allocation checks;
embedded contiguous-space and switch-peak validation remain necessary. Opus retains
fragment assembly storage only after fragmentation is observed, reuses it for a
fragmented stream, and frees it on unfragmented input/reconfiguration/cleanup.
LDAC/aptX/SBC optimization and their decode-vector benchmarks remain pending.

Immediate NVS remains mandatory. I2S DMA is currently 3 x 128 stereo frames and
the I2S ISR IRAM-safe option is disabled. Flash/cache interruption is a separate
unverified audio risk; a filter transition test does not prove it fixed.

## Hardware and Linux

VMware guest tools located Ubuntu; SSH works. The previously trusted `BDK SPEAKER` peer
matches the project audio/control UUIDs. Reconnect succeeded; BlueZ negotiated
LHDC V5 CIE `3a050000354c0102114000`; PipeWire reported 192 kHz/24-bit, **idle**.
No Firefox MPRIS player was found. This is the existing, unversioned firmware,
not the new host and not a proven match to this source. No music, decoder/DMA,
acoustic or coexistence result is claimed. Remembered sink volume exceeded 100%;
set a safe volume before playing music.

COM6 was positively identified by its USB serial, ESP32-D0WD-V3 revision 3.1,
factory MAC and 8 MiB flash ID, then correlated with the speaker through a
controlled codec change and UART configuration evidence. COM7/COM8 remain untouched.
The full original 8 MiB flash backup is retained with SHA-256
`c03b34c4e70b087508cb3189d34ca0b56d75cf24d39b6562f70794bdb729434f`.
The original OTA0 application, bootloader, partition table and first OTA selector
sector are preserved; trial writes are restricted to OTA1 and selector sector two.

The initial audio trial image passed complete write readback. Its readiness marker
was missed because observation used 921600 baud while the actual generated build
configuration was 115200. A subsequent read-only 115200 capture shows the trial's
`LIVE1` idle diagnostics. This establishes execution, not music continuity.
The failed raw capture remains preserved. The deployment tool now derives UART
baud from the candidate's actual build configuration and separates probe/audio receipts.

The independent runtime/GAP probe was then flashed into OTA1 with full image and
selector readback. Its `controller_identified` and `gap_state:2` markers were
observed on COM6. The eight identification and nine setup commands completed
without a controller error. Target ABI sizes: host 6672 bytes; runtime 10488 bytes
(includes host, ACL, reassembly, channel manager, TX descriptors and packet buffer).
Controller reports Classic 1021-byte/9-packet and LE 251-byte/10-packet separate
ACL pools. At identification, internal free heap was 216640 bytes and largest
block 110592 bytes. These exclude unimplemented profiles/audio and do not support
an equivalent-feature size/speed comparison. No advertising/scan was enabled and
no independent RF link was tested. Receipt: `_build/hardware/COM6-probe-trial.json`.

Subsequent `-WithGatt` probing added a diagnostic unbonded LE echo/counter service.
The server/bearer metadata adds 784 bytes on ESP32. A direct Linux kernel LE
connection negotiated MTU 517 and read the name. Linux BlueZ then completed
discovery, 200 request writes, 20 periodic readbacks, lengths 0/1/20/64/244/512,
a 512-byte reliable write and 208 received notifications in 29.6 seconds.
LE connected and BR/EDR disconnected were observed separately. Median/max write
RTT was 100.114/300.234 ms; the maximum notification interval was 707.809 ms.
These results do not prove sustained throughput, smooth audio, bonding or coexistence.
The exact image hash is bound to `_build/hardware/linux-gatt-live.json`.

Earlier BlueZ generic/separate-bearer attempts timed out; traces showed no Linux
HCI connection command. Selecting LE through `Device1.PreferredBearer=le` then
`Device1.Connect` succeeded. The experimental API was enabled temporarily only
with zero active Bluetooth connections, then the original daemon arguments were
restored. Failure receipts/captures remain private under the fixed `_build` paths.

After this probe, the audio trial was restored to OTA1. Complete image/selector
readback and its readiness marker were verified at 115200 baud. The board is
running `audio_candidate_integer_dsp1` with Bluedroid, not the independent probe.
Receipt: `_build/hardware/COM6-audio-trial.json`; original application remains intact.

Local SDK headers and ROM linker maps already expose standard HCI opcodes and
controller command-table symbols. No ROM dump/Ghidra database was created; use
decompilation for specific controller gaps, not as a substitute for host protocols.

## Fixed-path receipts

`_build/host/results.json`, `_build/host/linux_results.json`, `_build/dsp/results.json`,
`_build/dsp/linux_results.json`, `_build/lhdc/results.json`, `_build/linux/latest.json`.
Codec checks: `_build/codecs/aac_results.json`, `_build/codecs/opus_results.json`.
Probe build: `_build/esp32`; audio app: `_build/audio_app`.
Small original-source snapshots: `_build/dsp/baseline` and `_build/lhdc/baseline`.
Remote test sources reuse `~/.cache/native_bt/`; no SDK/media/credentials uploaded.
SSH dependencies are confined to `_build/python_deps`; compiler cache writes are
disabled in build wrappers. Latest generated size ~352 MiB plus ~71 MiB reference.

Latest focus: independent ATT/GATT server and bearer integration. The controller-only
probe defaults to configuring the controller with advertising and Classic scan
disabled. `build_probe.ps1 -WithGatt` opts into diagnostic LE advertising; it is
not the product control/OTA/audio profile. Its default
build no longer depends on the separate LHDC checkout; use `build_probe.ps1 -WithLhdc`
for the optional decoder object build. There is no decoder on the host loop.

Next: security/bond store and product GATT integration; SDP/AVDTP/A2DP/AVRCP; app handoff and live
coexistence. Flash-stall analysis preserving immediate NVS, LHDC A2DP integration,
and remaining codec work are still required. No full-stack speed/memory superiority
or production release readiness is established. See [release gates](RELEASE_READINESS.md).
