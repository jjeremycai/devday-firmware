# Flashing & Recovery

There is no over-the-air or portal update path: the only way to change the
firmware is USB flashing, which is deliberately unrestricted.

## Factory flash (Seeed line)

The packaged production image targets the EE04 display board. Older Seeed
XIAO ePaper driver-board units use a different CS/DC/BUSY/RESET pin map; build
one explicitly with `DISPLAY_BOARD=legacy tools/build.sh` before flashing its
app image at `0x10000`. The build prints the selected display board, and release
metadata records it as `display_board`.

Flash the merged image at `0x0`:

```sh
esptool.py --chip esp32s3 --port PORT --baud 921600 write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 devday-terminal-factory-1.0.1-rc1.bin
```

Then verify every shipped binary against `release/SHA256SUMS.txt` and run
serial `factory.check` on each unit (see docs/LINE_TEST.md).

## Flash map

| Offset | Region | Size | Purpose |
|---|---|---|---|
| `0x0` | bootloader | 0x7000 | 2nd-stage bootloader |
| `0x8000` | partition table | 0x1000 | `partitions.csv` |
| `0x9000` | nvs | 0x5000 | `Preferences` config |
| `0xe000` | otadata / boot_app0 | 0x2000 | boot selection initializer (always factory) |
| `0x10000` | factory | 3 MB | ships the RC |
| `0x310000` | ota_0 | 3 MB | reserved, unused |
| `0x610000` | ota_1 | 3 MB | reserved, unused |
| `0x912000` | nvs_keys | 0x1000 | NVS keys |
| `0x920000` | littlefs | 6.9 MB | content cache |

## Attendee reflashing (deliberately unrestricted)

Raw USB flashing is open. Erase and flash anything:

```sh
arduino-cli compile -b esp32:esp32:XIAO_ESP32S3_Plus MyApp
arduino-cli upload  -b esp32:esp32:XIAO_ESP32S3_Plus -p PORT MyApp
```

## Recovery

The ESP32-S3 USB-Serial-JTAG bootloader cannot be bricked by a bad app:

1. Hold the XIAO **BOOT** button, plug in USB, release BOOT.
2. Reflash the recovery image at `0x10000` or the full factory image at `0x0`:

```sh
esptool.py --chip esp32s3 --port PORT write_flash 0x10000 devday-terminal-recovery-1.0.1-rc1.bin
```

3. If configuration is suspect, hold **KEY1+KEY3** (D1+D4) at boot to factory-reset, or
   send `factory_reset` over USB.
