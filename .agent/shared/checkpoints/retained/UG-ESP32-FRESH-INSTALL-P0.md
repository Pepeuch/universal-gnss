# S3 autonomous first boot and preserving update

Lifecycle: RETAINED
Status: IMPLEMENTED (local P0 qualification; integration pending review)
Scope: DEPLOYMENT / VALIDATION
Canonical tracking: [TODO](../../../../TODO.md), [qualification](../../../../docs/analysis/ESP32_S3_FRESH_INSTALL.md), [artifact inventory](../../../../docs/analysis/ESP32_S3_FRESH_INSTALL_ARTIFACTS.json).
Source HEAD: 952eca0e65651f7f187c372a9dfe4fa80131c092 on fix/esp32-fresh-install.

## Contract and qualification

Unique autonomous P256 TLS before network/radio, startup hardware entropy, atomic
existing-format NVS identity/readback, bounded PSRAM workspace and wiping; read/OOM/
crypto errors remain explicit. Valid identity reused. Three erased HTTPS installs
and three confirmed electrical cuts each pass. HTTP hardware, same password/config
and byte-identical NVS/table/OTA/PHY pass. Earlier unchanged SDK/QEMU and native
crypto evidence remains in the report; do not repeat erase/cold matrices by default.

Real operator Chromium/Web Serial application-only updates pass current-device
comparison, approved target bytes, native installed-reference/schema binding,
OTA selection, readback and retained-region verification. Explicit Espressif USB
reset pulse corrects the pinned library's deassert-only routine; repeated browser
update returns pinned HTTPS without serial intervention. Publication authority and hardware compatibility remain independently checked.

Final clean952 BINbf308e2f435a955ce5c533febb5f522d7c62b4e88bfe057e94118479847af6a8
reproduces the durable published image exactly. Matching ELF744e7be9a57d0e10b5623afb8f0813d02c996a086853e8048383f37785125340
and MAP/config/hash inventory are archived outside Git on the operator host.
The resumed completion patch changes desktop Flasher/verification/CI/docs only;
no new shared core, C3 source, native feature, NVS schema or partition change.
Local79native,309policy,17Flasher,7update-controller and16Chromium scenarios pass.
Hosted seven workflows concern existing commit; uncommitted completion patch CI
must be confirmed after authorised publication of the source branch.

## Final operator state and remaining work

Original pre-session NVS restored ONLYat0x9000/24KiB; independent ROM readback
byte-identical before boot. Original boot/table/OTA/PHY are compatible and retained;
clean952 app stays installed. Original TLS certificate and root/status/resources200
verified; operator explicitly confirms usual credentials/dashboard. No temporary
credential is current. Never replay test-login scripts or older recovery snapshots.
Original radio/settings restored; free82555/largest34816/min42652/PSRAM8277880,
reduced pressure. This is preservation evidence, not a resource improvement.

F9P TX43/RX44/460800 identification and restart/re-identification pass, no reset ACK
or trusted cutoff asserted. Operator notes poor temporary contacts. Latest supplied
snapshot HPG1.32,3490observations,12satellites,51msposition age, zero UART faults,
fix_type2=kFix,rtk_mode1=kNone. Earlier RTK test is operator-reported; field validation
and endurance remain distinct. Keep coordinates, credentials and raw traces private.
Auth remains roughly39–42s. Resource refactor is next separately authorised work,
from main after P0 integration. No extra tests/flash solely for session restart.

Exact next action: maintainer reviews the uncommitted completion patch; commit/push/
publication require authorisation. Preserve local TODO/ROADMAP additions and the
resource audit. Reopen only for changed TLS/NVS/USB/update/native contracts, relevant
SDK/profiles, or contradictory evidence. No merge or firmware publication here.
