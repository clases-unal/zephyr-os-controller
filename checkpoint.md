# Project Checkpoint — High-Availability Thermal Control System
STM32L476RG (Nucleo) + Zephyr OS + ESP32. This file is a complete snapshot of every
decision made in the working session that started from the original project zip
(`stm32.zip`) plus `00-project-decisions-and-procedure.md`, `discussion.md`,
`PROJECT_STRUCTURE_GUIDE.md`, and `siguiente_paso.md`. If this conversation is lost,
paste this file back to Claude and say "resume from this checkpoint" — no need to
re-explain anything below.

Original source documents are still authoritative for anything NOT contradicted here.
Where this file conflicts with `00-project-decisions-and-procedure.md` or
`discussion.md`, **this file wins** — it supersedes them with the latest decisions.

---

## 1. Confirmed hardware decisions

### 1.1 Shift register is back (supersedes DEC-H-001)
The protoboard problem that caused DEC-H-001 (removal of the SN74HC595N) is
**resolved**. The project returns to using **one single SN74HC595N** (not two, unlike
the original `discussion.md` two-register design) to drive 8 LEDs (`Qa`-`Qh`).
When formalizing `docs/04-design-decisions.md`, rewrite DEC-H-001 in pure
"alternatives considered / decision" format (direct GPIOs vs. shift register →
shift register), **without narrating that the project went back and forth** — the
user explicitly does not want a changelog-style entry for this.

### 1.2 Final pin map (no conflicts, verified against STM32L476RG AF tables)

| Function | Pin(s) | Mode / AF |
|---|---|---|
| ADC (NTC) | PA0 | ANALOG |
| PWM fan | PB0 | AF2, TIM3_CH3 |
| Shift register (SN74HC595N) | PA5 (SCK), PA6 (LATCH), PA7 (MOSI) | AF5 — **physically SPI1**, even though the user calls it "SPI_2". Configure `&spi1` in the overlay/CMake, not `&spi2`. SPI2 on this MCU uses port B/C pins, not PA5-7. |
| OLED (SSD1306) | PC0 (SCL), PC1 (SDA) | AF4 — **I2C3** |
| ESP32 link | PC10 (TX), PC11 (RX) | AF7 — **USART3** |
| Keypad rows | PC7 (R0), PA9 (R1), PA8 (R2), PB10 (R3) | GPIO output |
| Keypad columns | PB4 (C0), PB5 (C1), PB3 (C2), PA10 (C3) | GPIO input, pull configurable |
| Shutdown button | PC3 | GPIO input, EXTI, software debounce |
| Heater keep-alive GPIO | PA4 | GPIO output |

Important notes:
- PA13/PA14 (SWDIO/SWCLK) were **avoided on purpose** — the user's first draft used
  them for the shift register and that would have broken ST-LINK programming.
- PC13 was **avoided on purpose** — it's shared with the Nucleo's onboard user button
  (B1), which the current `power_status_manager.c` uses via the `sw0` devicetree
  alias for the shutdown button. **Action needed**: since the physical shutdown
  button moves to PC3, either (a) repoint the `sw0` alias in the overlay to the new
  PC3 node so `power_status_manager.c` needs zero code changes, or (b) update the
  code to reference the new node directly. Preferred: **(a)**, simplest and lowest risk.
- Keypad rows/columns must be trivially swappable. This is already satisfied by the
  existing driver design (`matrix_keypad.c` reads `DT_NODELABEL(row_0..3)` /
  `DT_NODELABEL(col_0..3)`) — swapping roles only requires moving which physical pin
  gets which label in the overlay, no C code changes. Pull-up vs. pull-down is a
  one-flag change per column node (`GPIO_PULL_UP` ↔ `GPIO_PULL_DOWN` in the overlay).
  Overlay comments must spell this out explicitly.

### 1.3 Known existing bug independent of this session's changes
`power_status_manager.c` currently binds to `DT_ALIAS(sw0)` and implements only 2
press-duration zones (short = toggle `system_enabled`, long ≥3000ms = shutdown).
Per `discussion.md` §7.1, this is **semantically wrong**: short press should toggle
`ConfigState.hmi_mode` (handled by `ui_keypad_task`), not `system_enabled` directly,
and there should be a 1-3s dead zone. **Not fixed yet** — flagged for a later pass,
out of scope for the LED/OLED/pin-map work delivered so far.

---

## 2. LED representation — final design (single 8-output shift register)

8 outputs `Qa`-`Qh` (shifted out MSB/LSB order — decide and document explicitly in
the driver, doesn't matter which as long as it's consistent), one meaning each:

| Output | Color | Meaning |
|---|---|---|
| Qa | White | Kernel heartbeat — constant 1000ms blink |
| Qb | Blue 1 | Keypad+OLED interface health: solid = OK; slow blink 500ms = one of them lost |
| Qc | Blue 2 | ESP32 link: solid = OK; slow blink 500ms = connection lost |
| Qd | Red 1 | System shutdown / permanent alarm (see 2.1 below) |
| Qe | Green | Thermal bar: blink 500ms = COLD; solid = LOW (and everything above) |
| Qf | Yellow | Thermal bar: solid = MEDIUM (and everything above). **No dedicated blink state** — confirmed intentional, there aren't enough LEDs to give every state both a "current" and "passed" visual, so Yellow is always solid once reached. |
| Qg | Orange | Thermal bar: blink 500ms = HIGH; solid = CRITICAL (by sustained overtemp only, see §3) |
| Qh | Red 2 | Critical-cause indicator: blink 500ms = overtemperature CRITICAL (while Qe/Qf/Qg stay solid, frozen); solid = NTC sensor fault CRITICAL (while Qe/Qf/Qg all turn OFF) |

Bar logic (`Qe`-`Qg`) otherwise follows the original "progressive bar" rule from
`discussion.md` §2.3: LEDs for states already passed stay solid, the currently active
state blinks (except Yellow/MEDIUM, see above), states above the current one stay off.

### 2.1 Qd — three blink rates, final semantics
- **200ms** = Permanent Alarm (`SystemState.system_enabled == false`, see
  `discussion.md` §4.5). Blinks **indefinitely** — the only way out is a physical
  power cycle after human intervention on the NTC wiring; nothing in software clears
  this blink.
- **500ms** = SHUTDOWN in progress. Blinks only while the shutdown sequence is
  running, then turns off along with everything else once all tasks finish their
  cleanup (see §4 — no NVS write to wait for anymore).
- **1000ms** = reserved, unused for now.
- The old "keep-alive mirror" LED (§4.6 of `discussion.md`, previously a candidate
  for Registro 2 Q5) is **dropped entirely**. PA4 (the real keep-alive GPIO) is the
  only signal; there is no LED mirroring it anymore.

During SHUTDOWN: **everything else turns off** — heartbeat (Qa), Qb, Qc, and the
entire thermal bar (Qe-Qh) — leaving only Qd blinking at 500ms until shutdown
finishes, then Qd turns off too (all LEDs off = powered down/idle).

---

## 3. Threshold / hysteresis / CRITICAL logic — final design

### 3.1 Four editable thresholds (was three)
`ConfigState` gains a 4th field: `threshold_critical` (float), editable via keypad
exactly like the other three, with the same 2°C fixed hysteresis margin applied to
it as to the others (see `discussion.md` §4.2 for the asymmetric hysteresis rule:
instant on the way up, `threshold - margin` on the way down).

Proposed default: **`threshold_critical = 70.0°C`** (10°C above the existing default
`threshold_high = 60.0°C`). Not yet explicitly confirmed by the user as a final
number — flagged as adjustable, but implemented as the working default.

A generic hysteresis evaluator (single function/structure iterating over an ordered
list of `{threshold_value, resulting_state}` pairs) replaces having 4 near-duplicate
`if` chains — this was an explicit user request ("crea alguna estructura que agrupe
para todos los umbrales si ya pasó o no dicho margen de temperatura").

### 3.2 CRITICAL entry — two independent causes, as before
1. **Sustained overtemperature**: entered **instantly** when
   `current_temperature >= threshold_critical` (no timer gate on entry — the earlier
   idea of "N seconds sustained in HIGH before escalating" was dropped per the
   user's correction "el temporizador es solo cuando está en critical").
2. **NTC sensor fault**: entered instantly on ADC out-of-range reading (existing
   failsafe logic), independent of temperature value.

### 3.3 What the 20-second timer actually does (resolves `discussion.md` §9.1.3)
The user's clarification ("the timer only applies while already in CRITICAL") is
interpreted as: **once CRITICAL-by-overtemperature is active**, a 20-second timer
starts. If temperature has **not** dropped back below `threshold_critical - 2°C`
(hysteresis) by the time the timer expires, the system additionally **de-authorizes
the external heating plant** by dropping the PA4 keep-alive GPIO — this is exactly
the open point `discussion.md` §9.1.3 left pending ("¿debe cortarse el keep-alive en
CRITICAL por sobretemperatura, y con cuánta tolerancia?"). If temperature recovers
before the timer expires, CRITICAL clears normally and the timer resets without ever
touching the keep-alive line.

This does **not** apply to CRITICAL-by-sensor-fault, which already has its own
recovery path via the Watchdog (existing design, §4.3/4.4, untouched).

**This interpretation is a best-effort reading of an ambiguous instruction — flag
explicitly to the user for confirmation once they're back, it's the single point in
this whole session most likely to need correcting.**

### 3.4 Architecture note / known deviation
`discussion.md` §5.3 places temperature→threshold classification inside
**Temperature Manager**. The actual code has always placed it inside **Cooling
Manager** (`compute_threshold()` in `cooling_manager.c`), which already works and is
tested. To avoid destabilizing what works, the 4th threshold + hysteresis + CRITICAL
+ 20s-timer logic was added **in `cooling_manager.c`**, keeping the existing
(deviated-from-spec) architecture rather than moving classification into
`temperature_manager.c`. Worth reconciling in `02-firmware-architecture.md` as a
documented deviation rather than silently ignoring it.

---

## 4. NVS — reversed decision

**NVS will NOT be implemented.** `prj.conf`'s `CONFIG_NVS`, `CONFIG_FLASH`,
`CONFIG_FLASH_MAP`, `CONFIG_SETTINGS` are removed (they were enabled but unused —
`config_state.c` / `telemetry_state.c` never called the NVS API; every boot silently
reset thresholds and counters to defaults). This is documented as a **future
improvement** in `04-design-decisions.md` / the "Section 9" future-work list, listing
exactly which fields would need it: `threshold_low/medium/high/critical`,
`system_boot_count`, `error_count[4]`, `ntc_consecutive_failures`.

**Consequence for SHUTDOWN**: since there's no flash write to wait for anymore, the
SHUTDOWN sequence (`EVENT_SHUTDOWN_REQUESTED`) is redefined as **purely a clean-stop
sequence**: every thread reacts to the event, stops its own hardware safely (fan PWM
to 0%, keep-alive GPIO off, shift register cleared except Qd's 500ms blink, UART
told the link is going down if convenient), and the system settles into an idle loop.
No persistence step blocks this anymore.

---

## 5. ESP32 communication — confirmed gaps to fix (not yet implemented)
Verified by reading `esp32_comm_manager.c`: there is currently **no heartbeat, no
second UART RX thread, no `EVENT_ESP32_DISCONNECTED`**, and
`TransmissionState.esp32_connected` is hard-set to `true` once at boot and never
re-evaluated. `MSG_TYPE_CONFIG` is also never sent. This explains why the user never
saw "connected/disconnected" messages on the serial monitor — it's not a bug, the
feature was simply never built. **User confirmed: implement it fully** (heartbeat
every 5s, dedicated RX thread, disconnect detection, resend of pending
config/error on reconnect) — not yet done, queued as step 4 of the delivery plan
below.

Telemetry parameters in code (2000ms / 0.5°C delta) match
`00-project-decisions-and-procedure.md` DEC-F-003, **not** the older 10s/2°C figures
in `discussion.md` — the newer decisions doc is authoritative; `discussion.md` is
outdated on this specific point only. Note this when formalizing `docs/`.

---

## 6. OLED — root cause found, fix plan confirmed

Root cause confirmed by reading `ui_keypad_task.c`: the display was **never actually
initialized** — the code has `display_ok = false;` hardcoded with a comment saying
framebuffer init was deliberately disabled to avoid a `k_malloc` dependency. It's not
a wiring or I2C electrical problem (though the I2C bus itself was also moved from
I2C1/PB6-7 to I2C3/PC0-1 as part of the pin remap above).

Confirmed fix plan (user approved):
1. Get the real device via `DEVICE_DT_GET(DT_NODELABEL(ssd1306))` instead of the
   unset `display_dev` pointer.
2. `device_is_ready()` check.
3. `display_blanking_off()` + `cfb_framebuffer_init()`.
4. Add `CONFIG_HEAP_MEM_POOL_SIZE=2048` (or similar) to `prj.conf` — this was the
   actual reason framebuffer init was avoided originally (no heap configured).

Physical display specs (from user, secondhand memory): SSD1306, likely 128×64,
0.96", powered/driven at 3.3V. User is not certain about internal pull-ups on the
specific modules on hand; the overlay already forces internal `bias-pull-up` on
I2C3 pins, which should be enough either way. Screen layout (monitor mode / config
mode) is being **designed by Claude** as part of the OLED fix, since the user
explicitly said they don't have a defined layout — present it for approval, don't
just silently assume it's final.

---

## 7. Documentation & repository structure

### 7.1 `.gitignore`
Already created and delivered (`/mnt/user-data/outputs/.gitignore`). Goes at the
**repository root** (`zephyr-os-controller/.gitignore`), covers `.pio/`, `.vscode/`,
`.venv/`, build artifacts, OS junk. Already-tracked ignored files need
`git rm -r --cached <paths>` once, after adding the file.

### 7.2 Reorganization (approved by user, not yet executed at time of this checkpoint)
```
zephyr-os-controller/            ← new repo root (user chose this name)
├── README.md                    ← merged from stm32/README.md, ≤1 page
├── .gitignore
├── docs/
│   ├── 01-system-specification.md
│   ├── 02-firmware-architecture.md
│   ├── 03-state-machines.md
│   ├── 04-design-decisions.md
│   ├── 05-validation-plan.md    ← functional/software tests ONLY, electrical
│   │                               tests explicitly out of scope (no multimeter/
│   │                               oscilloscope/logic analyzer available)
│   └── diagrams/                ← Mermaid sources, one color scheme per diagram
├── firmware/
│   ├── stm32/                   ← current stm32/zephyr-os/* content moves here
│   │                               1:1 (platformio.ini stays at this level)
│   │   └── README.md            ← NEW: board-specific config summary (pins,
│   │                               peripherals, protocol spoken) — user explicitly
│   │                               asked for one per firmware, not just a root README
│   └── esp32/
│       └── README.md            ← NEW: placeholder, documents the *interface*
│                                   contract already defined (packet format, CRC16,
│                                   what the dashboard should show) and marks
│                                   internal ESP32 architecture as future work
│                                   (DEC-E-001)
├── hardware/
│   ├── wiring-diagrams/
│   └── bom.md
└── references/
    ├── datasheets/
    └── zephyr-docs/
```
`errores.md` and `consideraciones.md` (currently inside `zephyr-os/`) get their
content **migrated** into `docs/02-firmware-architecture.md` and
`docs/04-design-decisions.md` respectively, then the original files are deleted
(not kept as a separate bitácora — user confirmed "Migrar").

DEC-H-001 gets rewritten in pure options-format (see §1.1). Everything else in
`00-project-decisions-and-procedure.md` and `discussion.md` stays as historical/
working reference until formally superseded by the `docs/` files.

---

## 8. Delivery plan and status

| # | Item | Status |
|---|---|---|
| 1 | Overlay + `prj.conf` with final pin map | **Delivered this session** |
| 2 | LED Representation Manager redesign (single shift register) | **Delivered this session** |
| 3 | OLED fix + screen layout | **Delivered this session** |
| 4 | ESP32 Comm Manager: heartbeat + RX thread (single-thread poll loop, see §5 note below) + degraded mode | **Delivered** |
| 5 | Improved comments: `system_state.h/.c`, `telemetry_state.c` updated this round. `ntc_sensor.*`, `matrix_keypad.*`, `power_status_manager.*`, `main.c`, `temperature_manager.c` were already well-commented before this session and were left untouched. | **Delivered** (scope as noted) |
| 6 | Folder restructuring: creation script + migration script (`01-migrar-firmware.sh`) + per-firmware READMEs delivered. **Not executed** — these are shell scripts for the user's own Arch Linux machine, Claude cannot run them there. | **Delivered (scripts), execution pending on user's machine** |
| 7 | Formal `docs/` 01-05 + Mermaid diagrams (6 FSMs, colored) | **Delivered** |

Also delivered as a necessary side-effect of item 4: `protocol/uart_packet.h/.c` gained
`PACKET_TYPE_HEARTBEAT` and a full byte-by-byte RX parser (`uart_packet_parser_*`) —
previously the protocol only supported building outgoing frames, there was no way to
parse anything incoming at all.

Supporting state-layer changes made as a prerequisite for item 2 (not originally
one of the "top 3" but required for the LED bar to have real data to show):
- `control_state.h/.c`: added `THRESHOLD_CRITICAL` to the enum, added
  `critical_cause_t` (`NONE` / `OVERTEMP` / `SENSOR_FAULT`), added
  `time_in_critical_ms` tracking.
- `config_state.h/.c`: added `threshold_critical` field + default value.
- `cooling_manager.c`: extended `compute_threshold()` with the 4-threshold hysteresis
  evaluator, CRITICAL entry logic, and the 20s post-CRITICAL keep-alive cutoff timer
  (§3.3).
- `heater_simulation_task.h/.c`: added `heater_simulation_set_authorized(bool)` so
  `cooling_manager` can revoke the keep-alive line without reaching into GPIO
  registers directly from another module (keeps ownership of the PA4 pin inside its
  own task, consistent with the rest of the codebase's style).
- New driver: `drivers/shift_register.c/.h` — generic SPI-based SN74HC595N driver
  (didn't exist before; `discussion.md` §10.1 already planned this file, it was just
  never created because the register was removed by DEC-H-001 before implementation).

---

## 9. Open questions still worth re-asking the user if this checkpoint is resumed
1. Confirm or correct the §3.3 interpretation of "the timer only applies in
   CRITICAL" — implemented as a 20s post-entry grace period that cuts the keep-alive
   line if overtemperature CRITICAL doesn't clear in time.
2. Confirm `threshold_critical` default of 70.0°C, or provide a different number.
3. Review the OLED screen layout once shown (not yet designed at checkpoint time —
   will be delivered alongside the OLED fix).
4. Decide what to do about the pre-existing `power_status_manager.c` semantic bug
   (§1.3) — fix now or later.
