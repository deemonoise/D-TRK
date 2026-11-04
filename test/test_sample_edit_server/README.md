# Native SampleEdit session integration

```sh
bash test/test_sample_edit_server/run.sh
```

The runner extracts the unchanged production `SampleEditSession` block from `src/teensy/bank.cpp`
and compiles it with real wire codecs and `EvQueue`. StoreJob, synth/output, interrupt exclusion,
preview and reply transport are fakes. This tests session/admission/cancellation/cache behavior;
the real StoreJob PCM, filesystem and normalization paths have separate `test_sample_store` tests.
It does not compile the entire Teensy bank translation unit or access USB/SD/PlatformIO.

Both compilation and execution use AddressSanitizer/UndefinedBehaviorSanitizer. The existing
`.pio/libdeps/native/Unity` dependency is required. `.inc` keeps this explicitly configured host
fixture outside PlatformIO's ordinary native `.cpp` discovery.

Cases cover exact typed token+wire-seq+payload retries (Edit 23 bytes / Tempo 17 bytes), sequence reuse with a new token, stale source and
bad argument rejection before I/O, transport/job exclusion, park/reset/queue-clear before startup,
future queued events during every step and terminal path, result registration, cancellation using
the original reply identity, late cancellation, silent Hello reset and unrelated bank-job survival.
Stretch mode validates ratio/options/output overflow before taking the card. Tempo analysis shares
the same offline session, but never registers a PCM output; its success may validly report unknown.
