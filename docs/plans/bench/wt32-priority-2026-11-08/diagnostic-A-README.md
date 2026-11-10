WT32 personal diagnostic A: fully isolated snapshot; NOT a normal release.

Firmware: .pio/build/wt32/firmware.bin
SHA256: 022991edcddaf640f59823ca1b97645747314d13a205dea542fd52fe2d890cad
Build environment: wt32, QIO_QSPI N16R2, Arduino3.3.12, personal first track=P12bit10.
Version e7efe3b-autoA; actual dirty source hashes retained in production-source-sha256.json.
Frozen A core_ram.cpp SHA25645039c5292b2dce8d9341ebcd831a7e9564fa011aab06fc585813158a8979edb
matches /private/tmp/dtrk-policy-A-current-core exactly. No B policy mixed into this image.

Original setup/autoload/pullSamples retained. The original loop remains, with one diagnostic
coordinator call inserted before input processing. No embedded project/audio assets.
On first loop: verify autoload+sample readiness, project TUTU-TUDUTU, framebuffer/RVB buffers,
and exact A placement SynthINT/SeqINT/RVBPS. Invalid state aborts to normal Wi-Fi dialog.
Physical input is drained through App; all edits/touch ignored. Play or status transport tap cancels.

Sequence: normal FILE -> SYSTEM/Diagnostics with CPU profile row selected; transport locked;
post actual Stop, disarm REC, SetSong(false), SelectPattern0. Await queue-empty+stopped under
bounded project mutex. Seed actual Sequencer0x5EED; canonical FNV1a over saveProject(ByteSink)
under stopped project mutex (no file/heap). Park actual audio, reseed actual Synth, unpark.
Post actual StartStop, await playing P01/LOOP with no count-in. FollowSalt also0x5EED in this image.
Warm8sec. Three consecutive8sec profiles call existing FileScreen.toggleProfile wrappers.
Timestamp for next window captured AFTER the previous SDappend and next profile start.

Original firmware first profile line/metadata preserved; extra lines identify A, window1/2/3,
canonical model FNV/bytecount/seed, actual UI pushed frames/fps and sampled500ms CPU windows
with exclusive yellow/red counts and stalls. Observer copies existing pollCpu result; no extra
takeLoad or CPU threshold change. Per-window start/end are UI timestamps; audio profiler applies
at block boundaries, so expect several-ms boundary skew and roughly16CPU samples/window.

After final window or any error/cancel: discard active partial profile, diagnostic reason in
/diag/autoprof.txt, post Stop, await stop ACK (3sec bounded), unlock diagnostic input, open normal
FILE Wi-Fi transfer. Network/web polling continues via normal App.tick; familiar SSID/IP flow.
Controller stops permanently after this single run; no automatic repeat.

Controller never writes project files or settings. Diagnostic image compiles idle autosave/settings
writers out. Only profile appends and completion/error diagnostic log are new file writes, outside
measured windows. Original setup boot log/cache/sample pull behavior is retained.

Tests: diagnostics/coordinator_test.cpp,17mock/stat scenarios under ASan/UBSan. RED logs before
controller implementation and before initial-cancel fix; final coordinator-green.log PASS.
source-guards.log proves original pollCpu body unchanged apart from observer, one takeLoad.
GCC build-A-final-ready.log PASS (pre-existing ProjScreen offsetof warnings only). UncleBob gates invoked;
C++ unsupported => No source files, NOT reported as passing.

Limitations: original fatal pre-App/Project boot allocation handling is intentionally unchanged.
Normal Wi-Fi open has an additional bounded stopped ACK guard. It still depends on card/radio/NVS; after a failed open the
controller exits finitely with input restored and an error, rather than repeatedly retrying forever.
SDappend reports the existing API's open/write status; no new backend/readback protocol introduced.
Root alone owns OTA/devices. Builder has not flashed anything. B must clone this controller and
replace only approved core policy/version/precondition (SynthINT/SeqPS/RVBINT), then rebuild.

Manual-GUI caveat approved by root: Card row remains visible and calls SD.totalBytes()/usedBytes()
through FatFS f_getfree on each redraw, exactly like current manual SYSTEM. These calls did not
exist before the UI wave. Physical card IO every frame is NOT established (FatFS may cache free
cluster counts). No diagnostic caching is applied: A/B deliberately retain identical GUI behavior.
