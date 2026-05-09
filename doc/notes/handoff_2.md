# Handoff 2 — Pi Zero W WiFi / SDIO CMD5 Debug

**Date:** 2026-05-09  
**Status:** Zephyr `sd_init()` now enumerates CYW43438 SDIO over a BCM2835 GPIO bit-bang fallback, and a CMD53 backplane smoke test reads the chip ID. SDHCI hardware still times out.

## 2026-05-09 Update

The `sdio_probe` sample originally proved a zerowi-style bit-banged SDIO probe
works, and `drivers/sdhc/sdhc_bcm2835.c` now routes SDIO enumeration plus CMD52
and CMD53 through the same raw GPIO path.  Early hardware run:

```text
[bb] CMD5 arg=0x00000000 rsp_bits=48 rsp=3f a0 ff ff 00 ff
[bb] CMD5 arg=0x00200000 rsp_bits=48 rsp=3f a0 ff ff 00 ff
[bb] CMD3 arg=0x00000000 rsp_bits=48 rsp=03 00 01 00 00 eb
[bb] CMD3 selected RCA arg=0x00010000
[bb] CMD7 arg=0x00010000 rsp_bits=48 rsp=07 00 00 1e 00 a1
[bb] CMD52 arg=0x00000000 rsp_bits=48 rsp=34 00 00 10 32 45
[bb] CMD52 CCCR/SDIO rev data=0x32
[bb] CMD52 arg=0x00001000 rsp_bits=48 rsp=34 00 00 10 02 13
[bb] CMD52 card capability data=0x02
```

The SDHCI comparison immediately afterward still fails:

```text
sdhc cmd5 failed: ret=-116 int=0x00018000 ... clk=0x3947 ... pwr=0x0f
```

Conclusion: the CYW43438 is powered, clocked, in SDIO mode, and responding on
GPIO34-GPIO39 when those pins are driven manually.  The remaining interface
blocker is the BCM2835 Arasan SDHCI path, not basic Wi-Fi module bring-up.

Latest driver-backed `sd_init()` + Broadcom backplane smoke-test output from
`/tmp/sdio_probe.log`:

```text
card type=1 io_functions=2 ocr=0xa0ffff00 cccr=0x00000021
func0 manf=0x02d0 code=0xa9a6 func=0x0c max_blk=32 max_speed=0x58
func1 CIS pointer=0x001000
func0 bytes @0x001000: 20 04 d0 02 a6 a9 21 02 0c 00 22 2a 01 00 00 00
func1 manf=0x02d0 code=0xa9a6 func=0x0c max_blk=64 ready_timeout=0
backplane IOEN=0x06 IORDY=0x02
backplane chip-id raw: a6 a9 41 15
backplane clock csr after request=0x68
brcm_probe_backplane() returned 0
SDIO probe complete
```

The generic function-1 CIS pointer is sometimes observed as `0x002000` with
zero-filled bytes after a warm/dirty SDIO state.  `subsys/sd/sdio.c` now bounds
the tuple scan and rejects zero-length tuples so that bad CIS data returns
`-EINVAL` instead of spinning forever.  A zerowi-style CMD52 soft reset before
the first bit-banged CMD0 was added in the BCM2835 driver to improve cold/clean
enumeration consistency.

---

## What Is Working

| Item | Status |
|------|--------|
| GPCLK2 (WIFI_CLK) on GPIO43 / GPIO6 | ✓ 32.854 kHz confirmed by logic analyzer |
| GPIO34-39 in ALT3 (SD1_CLK/CMD/DAT0-3) | ✓ GPFSEL3 = 0x3ffff000 |
| GPIO43 ALT0 (GPCLK2) | ✓ GPFSEL4[GPIO43]=4 |
| WL_ON (GPIO41) = HIGH (chip not in reset) | ✓ |
| SDHCI clock = 400 kHz (clkpwr = 0x3947) | ✓ |
| SDHCI power = 3.3V (pwr = 0x0f) | ✓ |
| SDHCI PRESENT_STATE card-inserted bit | ✓ 0x01FF0000 |
| SDHCI generates HW timeout on CMD5 | ✓ int = 0x00018000 |
| Bit-banged CMD5 response | ✓ 48-bit response observed |
| Bit-banged RCA/select/CMD52 function 0 | ✓ CMD3/CMD7/CMD52 reads observed |
| Zephyr SDIO `sd_init()` over bit-bang fallback | ✓ function 0 + function 1 enumerate in clean run |
| Bit-banged CMD53 data path | ✓ 4-byte backplane chip-id read works |

---

## The Core Problem

**CMD5 (SDIO_SEND_OP_COND) always times out — `int=0x00018000` (bit 16 = Command Timeout Error).**  
The SDHCI hardware transmits CMD5 on the bus and fires a hardware timeout waiting for a response.  
The CYW43438 never pulls CMD line LOW to start its response.

Every software-level fix has been verified:
- Shadow-register bug fixed (single 32-bit write for TRANSFER_MODE+COMMAND)
- R4 command flags correct: `SDHCI_CMD_RESP_SHORT` (no CRC/index check)
- Write delay correct: `(2 * 1000000 / freq) + 1` µs after each register write
- WL_ON power cycle tested (not the issue — "direct" test without power cycle also fails)
- EMMC clock source: PLLD_PER / 2 = 250 MHz, SDHCI divides to 400 kHz ✓

---

## Critical Discovery: zerowi Uses GPIO Bit-Banging, NOT SDHCI

The [zerowi](https://github.com/jbentham/zerowi) reference implementation (the only known working bare-metal SDIO driver for this chip on Pi Zero W) explicitly switches GPIO34-39 **out of ALT3 and into raw GPIO mode** before doing any SDIO:

```c
// srce/zscan.c — sd_setup(), called BEFORE gpio_out(WLAN_ON_PIN, 1)
void sd_setup(void)
{
    gpio_set(SD_CLK_PIN, GPIO_OUT, GPIO_NOPULL);  // GPIO34: output, NO pull
    gpio_set(SD_CMD_PIN, GPIO_IN,  GPIO_PULLUP);  // GPIO35: input,  pull-up
    gpio_set(SD_D0_PIN,  GPIO_IN,  GPIO_PULLUP);  // GPIO36: input,  pull-up
    gpio_set(SD_D1_PIN,  GPIO_IN,  GPIO_PULLUP);  // GPIO37: input,  pull-up
    gpio_set(SD_D2_PIN,  GPIO_IN,  GPIO_PULLUP);  // GPIO38: input,  pull-up
    gpio_set(SD_D3_PIN,  GPIO_IN,  GPIO_PULLUP);  // GPIO39: input,  pull-up
}
```

The zerowi author stated: *"In the absence of the necessary documentation, writing code for the 'Arasan' SD controller on the Raspberry Pi would be quite fraught, so I decided to use direct control (bit-banging)."*

Our Zephyr driver (`sdhc_bcm2835.c`) keeps GPIO34-39 in ALT3 (SDHCI / Arasan hardware) and never bit-bangs. This is the likely reason CMD5 fails — the BCM2835 Arasan SDHCI at 0x20300000 may have undocumented quirks preventing correct SDIO operation with CYW43438.

---

## Datasheet Findings (CYW43438, Doc 002-14796)

### SDIO Mode Selection (Strapping)
- `SDIO_DATA_2` (GPIO38) sampled at POR deassertion (WL_REG_ON ↑)
- Default = **1** (internal pull-up) → SDIO mode ✓
- Pull LOW → gSPI mode
- Pi Zero W PCB: GPIO38 has external pull-up → SDIO mode confirmed

### Required Pull-ups (Table 17 Note)
> "Per Section 6 of the SDIO specification, 10 to 100 kΩ pull-ups are required on the **four DATA lines and the CMD line**."
- Pi firmware applies these via GPIO GPPUD (GPIO_PULLUP) on GPIO35-39
- zerowi explicitly calls `gpio_pull(pin, GPIO_PULLUP)` on CMD+DATA
- Our pinctrl may **not** be enabling pull-ups on these pins — needs verification

### Power-Up Timing (Section 20)
- Hold in reset for **≥2 sleep clock cycles** (~61 µs at 32.768 kHz) after VBAT/VDDIO reach 90% VH
- Wait **≥150 ms** after VDDC/VDDIO before initiating SDIO access
- Internal POR holds chip in reset for **≤110 ms** after VDDC/VDDIO exceed 0.6V
- At least **10 ms** between consecutive WL_REG_ON toggles (CBUCK discharge time)

### SDIO Initialization Sequence (from zerowi sdio_init)
zerowi's working sequence (bit-banged):
```
1. CMD52 read  func=0 reg=0x06 (I/O Abort)      [chip may be in prior SDIO state]
2. wait 20 ms
3. CMD52 write func=0 reg=0x06 val=0x08          [SDIO soft-reset, RES bit]
4. wait 20 ms
5. CMD0  arg=0                                    [GO_IDLE_STATE]
6. CMD8  arg=0x1aa
7. CMD5  arg=0         (first pass, get VDD info)
8. CMD5  arg=0x200000  (second pass, assert voltage)
9. CMD3  arg=0         (get RCA)
10. CMD7 with RCA      (select card)
... CMD52 bus/backplane setup ...
```

Key: zerowi does **CMD52 SDIO soft-reset first**, before CMD0, because the chip was previously initialized by Pi firmware.

---

## Recommended Next Steps (Priority Order)

### Option A — Bit-bang CMD5 test (done)
`sdio_probe/src/main.c` now:
1. Switches GPIO34-39 from ALT3 to raw GPIO (GPIO_IN/GPIO_OUT)
2. Enables pull-ups on GPIO35-39 via GPPUD register
3. Bit-bangs CMD52 reset + CMD0/CMD8/CMD5/CMD3/CMD7/CMD52
4. Confirms the CYW43438 responds through function 0

CMD5 succeeds via bit-bang but not SDHCI, confirming the SDHCI path is the
issue.

### Option B — Fix pinctrl to enable pull-ups on GPIO35-39
Check the DTS pinctrl node for the SDHCI device. If `bias-pull-up` is not set on GPIO35-39, add it. The BCM2835 GPPUD registers may need to be set before SDHCI init. This could be what prevents the chip from seeing valid CMD line levels.

### Option C — Implement Zephyr bit-bang SDIO driver (in progress)
The first two stages are now in `sdhc_bcm2835.c`: command-only init/CMD52 and
single CMD53 byte-mode reads/writes use the bit-banged GPIO path. This is enough
for Zephyr SDIO enumeration and for a small Broadcom backplane read. Next, move
the Broadcom setup out of the diagnostic sample into a Wi-Fi-facing driver path,
then extend/test larger CMD53 block transfers for firmware and NVRAM upload.

---

## File Locations

| File | Purpose |
|------|---------|
| `zephyr/drivers/sdhc/sdhc_bcm2835.c` | BCM2835 Arasan SDHCI driver |
| `zephyr/samples/boards/raspberrypi/rpi_zero_w/sdio_probe/src/main.c` | Diagnostic probe sample |
| `zephyr/doc/notes/run_sample.py` | Build+flash+serial capture script |
| `zephyr/doc/notes/rpi-zero-jlink.cfg` | OpenOCD config for JLink |
| `zephyr/doc/notes/cmds.gdb` | GDB load-and-run script |
| `references/zerowi/srce/` | zerowi reference implementation |
| `references/zerowi/srce/zscan.c` | Main init: sdio_init(), sd_setup() |
| `references/zerowi/srce/zw_sdio.c` | Bit-bang SDIO protocol |
| `references/zerowi/srce/zw_gpio.c` | GPIO/pull-up helpers |
| `references/infineon-yw43438-*.pdf` | CYW43438 datasheet |

---

## How to Run the Current Probe Sample

```bash
cd /Users/karaketir16/proj/zephyrWorkspace
python3 zephyr/doc/notes/run_sample.py \
    zephyr/samples/boards/raspberrypi/rpi_zero_w/sdio_probe \
    --timeout 30 \
    --log-file /tmp/sdio_probe.log
```

run_sample.py: builds with `west build`, kills stale openocd, flashes via JLink+GDB, streams serial at 115200 on `/dev/tty.usbserial-0001`.

---

## BCM2835 Register Reference

```
GPIO_GPFSEL0  0x20200000   GPIO function select 0 (GPIO0-9)
GPIO_GPFSEL3  0x2020000c   GPIO function select 3 (GPIO30-39)  ALT3=7 for SD1
GPIO_GPFSEL4  0x20200010   GPIO function select 4 (GPIO40-49)  ALT0=4 for GPCLK2
GPIO_GPLEV0   0x20200034   GPIO level register 0 (GPIO0-31)
GPIO_GPLEV1   0x20200038   GPIO level register 1 (GPIO32-53)
GPIO_GPPUD    0x20200094   Pull-up/down control: 0=off, 1=pulldown, 2=pullup
GPIO_GPPUDCLK1 0x2020009c  Pull-up/down clock reg 1 (GPIO32-53)

CPRMAN_BASE   0x20101000
CM_GP2CTL     0x20101080   GPCLK2 control  (target: 0x5a000291 = OSC, ENAB, GATE)
CM_GP2DIV     0x20101084   GPCLK2 divider  (target: 0x5a249f00 = DIVI=585, DIVF=0xF00)
CM_EMMCCTL    0x201011c0   EMMC clock control
CM_EMMCDIV    0x201011c4   EMMC clock divider (target: DIVI=2 → 250 MHz from PLLD_PER)

SDHCI_BASE    0x20300000   BCM2835 Arasan SDHCI (undocumented)
SDHCI_PRESENT 0x20300024   Present state: bit16=card, bit24=CMD, bits[23:20]=DAT
SDHCI_CLKCTL  0x2030002c   Clock control (16-bit at offset 0x2c)
SDHCI_PWRCTL  0x20300029   Power control (8-bit, 0x0f = 3.3V + power on)
SDHCI_INT_STATUS 0x20300030  Interrupt status: bit15=Error, bit16=CmdTimeout
```

---

## Key Signals for Logic Analyzer

| Signal | GPIO | Header Pin | Expected |
|--------|------|------------|---------|
| SD1_CLK | 34 | — | 400 kHz bursts during SDHCI cmd |
| SD1_CMD | 35 | — | CMD5 frame + response (or timeout) |
| SD1_DAT0 | 36 | — | Idle HIGH |
| WIFI_CLK / GPCLK2 | 43 / 6 | pin 31 | 32.854 kHz continuous |
| WL_ON | 41 | — | HIGH = chip active |
