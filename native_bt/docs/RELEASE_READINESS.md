# Independent host release gates

Updated 2026-10-28. **Development milestone; replacement firmware is not ready to ship.**

The existing audio application uses Bluedroid. The independent probe has no audio
or GATT profiles. Keep these deliverables distinct in GitHub branches, binaries,
release notes and test reports. A successful lower-layer probe does not migrate
the product application.

| Required gate | Evidence now | Remaining work |
| --- | --- | --- |
| Independently authored source | Owned C11 core; explicit source lists exclude BTstack/Bluedroid/NimBLE from probe | Continue provenance/dependency review for each new layer |
| HCI ownership and command lifecycle | Copied bounded ingress; general command queue; asynchronous transport lifetime and timeout tests | Production recovery and incoming controller flow control |
| Links and Basic L2CAP | Paired peers establish/configure/transfer fragmented data/disconnect through actual runtime APIs | External peer interoperability, traffic limits and target soak |
| Classic + BLE scheduling | Separate/shared credit accounting; independent-link admission tests | Simultaneous RF connections and sustained traffic |
| Controller setup/GAP | Nine-command configuration; explicit scan/advertising control; negative security replies | Outgoing procedures, discovery, privacy and procedure recovery |
| Pairing and bonds | Admission/security hooks only | Classic SSP/PIN/key lifecycle; LE SMP, secure connections, keys and persistent bonds |
| BLE application | Fixed-channel routing only | ATT/GATT, permissions, per-link CCCDs, notifications/indications, MTU, reliable writes, control/meter/upload/OTA migration |
| Classic audio discovery/control | Basic PSM channels only | SDP, AVDTP/A2DP, AVCTP/AVRCP, stream negotiation/media framing and worker handoff |
| Audio codec parity | Separate Bluedroid AAC/Opus memory changes and DSP tests | All codec configurations, LHDC V5 A2DP integration, lazy switch peaks and allocation failures on ESP32 |
| EQ persistence and playback | Immediate packed NVS commit; integer ramps; host tests | Power-loss persistence and physical continuous audio during repeated real flash commits |
| Interoperability/audio | No qualifying playback/coexistence run | Firefox music from Linux, verified Classic+LE bearers, continuous reads/writes/notifications at every negotiated codec/configuration |
| Feature coverage | Explicit architecture inventory | ERTM/LE credit channels and other requested profiles/roles; reject unsupported modes accurately |
| Resource advantage | Bounded disk outputs; reported object allocations | Compare RAM/CPU/latency against equivalent enabled features and workloads, including switch peaks |

Test receipts with source hashes are generated under `_build/host/`. Keep private
UART/flash/SSH artifacts under ignored `_build/`; publish only reviewed source and
sanitized evidence summaries. Do not upload the original flash backup or credentials.

The deadline does not turn an unimplemented gate into a pass. An early GitHub
checkpoint must say development/incomplete and must not replace a stable release.
