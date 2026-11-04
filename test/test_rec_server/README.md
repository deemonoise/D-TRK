# Native REC integration

Run from the project root:

```sh
bash test/test_rec_server/run.sh
```

This runs the portable `test_rec` suite and compiles the actual `rec_server.cpp` into a host test
translation unit with real `RecCapture`, `RecWriter`, protocol codecs and WAV/CRC code. Only the
Teensy Audio/Arduino API, filesystem, codec, PSRAM provider, bank job and link transport are fakes.
Both executables use AddressSanitizer and UndefinedBehaviorSanitizer. No PlatformIO or device access
is involved. The existing native Unity dependency under `.pio/libdeps/native/Unity` is required.

The integration cases live in `.inc`; `env:native` explicitly ignores `test_rec_server`, whose
runner supplies the required hardware stub include path. The ordinary portable
`test/test_rec/test_main.cpp` remains discoverable by PlatformIO.

Coverage includes PSRAM loss during recording and sealing, unread-ring discard, a freed allocation
after invalidation, STOP/level error results, preserved WAV prefix samples/size/CRC, keepalive and
Hello reset paths, disabled bank import, seal/write failure precedence, and healthy RESAMPLE/STOP
retry behavior. These are software proofs; a physical PSRAM fault, codec input and actual SD write
timings still require hardware validation.

The threshold case also runs repeated loop drains while Armed: no frames may reach the file until
triggering, then the WAV must contain exactly the last 20 ms of pre-roll, the trigger block and the
post-trigger blocks, with the matching stereo data and mono CRC.
