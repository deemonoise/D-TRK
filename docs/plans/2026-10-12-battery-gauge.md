# Battery gauge (INA226)

## Design

Hardware: INA226 module (shunt R100 = 0.1 Ω) in the battery's positive wire: LiPo+ → IN+, IN− → IP5306 B+. VBS to IN+. I2C on the same lines as the PCF8575 (port 0, GPIO 43/44, `Wire`), address 0x40. Positive current = discharge, negative = charging. See `docs/wiring.md`.

- **Driver** (`src/hw/battery.*`): polled once per second by the existing `trackio` task (the only user of `Wire`, so no bus contention). The task now runs when either the PCF8575 or the INA226 answers. No calibration register: current = shunt register × 2.5 µV / `kShuntOhm`; voltage = bus register × 1.25 mV. Last sample published through atomics.
- **Gauge** (`lib/core/src/battery_gauge.*`, pure, native-tested): open-circuit voltage V_ocv = V + I·R_int (R_int 0.15 Ω), LiPo OCV table → percent, EMA (τ 30 s) on V_ocv, current EMA (τ 5 s) for charging detection (I < −20 mA). While not charging the shown percent never rises. Low alert: once when the percent drops to ≤ 10 while not charging; re-armed above 15 % or while charging.
- **UI**: status bar, right side, before CPU: loop counter shrinks to 5 chars (`L9999`, beyond `L10K+`), then a 16 px battery icon (fill by level, bolt while charging) and `85%`. Colour: charging green, ≤ 10 % red, ≤ 20 % yellow, else dim. No sensor → nothing drawn. Toast `BATTERY LOW - SAVE` on the low alert. FILE → SYSTEM: read-only row `Battery` (`3.92V 120mA 85%`, `4.05V CHG 820mA 64%`, `NOT FOUND`).

## Task 1: gauge logic + tests

Files: create `lib/core/src/battery_gauge.h`, `lib/core/src/battery_gauge.cpp`, `test/test_battery/test_main.cpp`.

```cpp
namespace mt {
// Percent 0..100 of a LiPo cell at rest (open-circuit voltage), linear between table points.
int lipoPercent(float ocvVolts);

class BatteryGauge {
 public:
  static constexpr float kRint = 0.15f;      // cell + wiring, ohm
  static constexpr float kChargeA = -0.02f;  // current below this = charging
  static constexpr float kTauS = 30.f;       // voltage smoothing
  static constexpr float kTauIS = 5.f;       // current smoothing
  static constexpr int kLowPct = 10, kRearmPct = 15;
  // One sample: volts at the cell, amps (+ discharge, − charge), seconds since the previous one.
  // The first sample initialises the filters.
  void update(float volts, float amps, float dtS);
  int percent() const;      // -1 before the first sample
  bool charging() const;
  bool takeLowAlert();      // true once per crossing; clears itself
};
}
```

OCV table (V → %): 3.30 0, 3.50 5, 3.61 10, 3.69 20, 3.74 30, 3.77 40, 3.80 50, 3.84 60, 3.90 70, 3.98 80, 4.06 90, 4.18 100. Clamp outside.

Tests: table points and interpolation and clamping; IR compensation (3.70 V at 0.3 A reads as 3.745 V); first sample sets the value without smoothing; smoothing step; no rise while discharging (a higher voltage sample keeps the percent); rises while charging; charging detection with current EMA; low alert fires once at ≤ 10, not while charging, re-arms above 15; percent −1 before update.

Run: `pio test -e native -f test_battery`.

## Task 2: INA226 driver

Files: create `src/hw/battery.h`, `src/hw/battery.cpp`; modify `src/hw/trackio.cpp` (+ header comment).

```cpp
namespace hw {
struct BatterySample { float volts; float amps; uint32_t ms; };  // amps: + discharge, − charge
bool batteryPresent();                  // INA226 answered at boot
bool batteryLast(BatterySample& out);   // false until the first sample
// For trackio (owner of Wire): after Wire.begin; then from its task, cheap when not due.
bool batteryProbe();
void batteryPoll(uint32_t nowMs);       // reads once per kPollMs (1000)
}
```

- `kShuntOhm = 0.1f` with a comment (R100 on the module; R010 → 0.01f).
- Probe 0x40: manufacturer ID register 0xFE must read 0x5449; then write config 0x00 = 0x4527 (AVG 16, 1.1 ms bus and shunt, continuous). Print `battery: INA226 at 0x40` / `battery: INA226 not found`.
- Read 0x01 (int16, 2.5 µV/LSB) and 0x02 (uint16, 1.25 mV/LSB), big-endian. A failed read keeps the last sample.
- Publish volts / amps / ms through atomics (float bits in `std::atomic<uint32_t>` or a seqlock), readers on another core.
- `trackioBegin`: `Wire.begin` → `batteryProbe()` → PCF scan. Start the task if the PCF or the INA answered; the task reads buttons only with the PCF, calls `batteryPoll(millis())` every loop. Return value unchanged (true = PCF found).

Verify: `pio run -e wt32` builds.

## Task 3: UI

Files: modify `src/ui/app.h`, `src/ui/app.cpp`, `src/ui/proj_screen.h`, `src/ui/proj_screen.cpp`.

- `App`: member `mt::BatteryGauge gauge_`, `pollBattery(now)` next to `pollCpu` (every 1000 ms: take `hw::batteryLast`, skip if `ms` unchanged, dt from sample ms, update gauge, set `dirty_` when percent / charging / colour changed; `takeLowAlert()` → `toast("BATTERY LOW - SAVE")`). Accessor for the SYSTEM row (gauge const ref).
- `drawStatus`: loop at x 312, `L%lu` up to 9999, else `L10K+`; battery icon at x 360 (body 14×10 at y 7, nub 2×4), fill width by percent, bolt while charging; `%d%%` at x 380 (ends before CPU at 416). Nothing when `!hw::batteryPresent()` or percent < 0. Drawn after the toast check (hidden by toasts, like the CPU field).
- FILE → SYSTEM: row `kBattery` before `kCpuProf`, read-only.

Verify: `pio run -e wt32` builds.

## Task 4: docs

`docs/manual.md`, `docs/manual_ru.md`, `README.md`, `README_ru.md`: status bar battery field (colours, bolt, toast), FILE → SYSTEM row Battery, loop counter limit `L10K+`.
