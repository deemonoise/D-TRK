WT32 temporary C2 diagnostic: controlled Card cache experiment.

Based on final /private/tmp/dtrk-wt32-autoprof-B2. Version e7efe3b-autoC2, same text width as B2; policy C-cardcache.
Firmware SHA80893190b4f653caeeb3d2cd78d445b574f670415ccb8acf12f179c0a294546f. GCC build-C-ready.log PASS.

All controller/adapter/stats/ProjectFnv headers and both core_ram files are byte-identical to B2. Actual mount, autoload, sample readiness, placement guard SynthINT/SeqPS/RVBINT, Stop ACK, seed, Paused,8s warm and3x8s windows are unchanged. Semantic model FNV expected4363edfc; raw hash also reported, hash_excludes_crc1.

The original Card formatter body is unchanged. At debugProfilePage, after setup/mount/autoload and before warm/profile, it executes once into a32-byte cache, then its callback is replaced with a cached-string formatter. There is no refresh timer and no Card capacity/used reads inside measured windows. The same Card string and row layout remain visible. Cache stores the whole formatted row, so it removes both repeated getters and redundant numeric formatting; this diagnoses Card-row work, not physical SPI cost alone. No Card/refresh framework was added to production.

Default/absent card is nonfatal and shows NO SD CARD. Original zero-capacity/used>total formatting is preserved. Snapshot remains fixed until this diagnostic reboots; this is not a production free-space-refresh design.

Regression diagnostics/run_card_cache.py extracts and executes the REAL Card lambda from FileScreen.cpp against counted SD boundaries. RED:3/5 failures and960 total/used reads for960 renders. GREEN:5/5 ASan/UBSan, exactly1 read pair before windows and0 extra during3x320 renders, repeatedcapture/noSD/clamp/emptycallback/truncation cases. No source-text assertion substitutes for these behavior checks.

Independent final source/bin/flags review by editor_song_port: CLEAR. Shared helpers and source/binary hashes are recorded in diagnostic-C-manifest.json. UncleBob crapper/dryer/mutator do not support C++; N/A, not a passing zero-function gate.

Root alone owns OTA/device operations. Builder did not touch production sources or any device.
