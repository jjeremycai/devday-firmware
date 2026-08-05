# Flashing & Recovery

## Factory flash (Seeed line)

Flash the merged image at `0x0`:

```sh
esptool.py --chip esp32s3 --port PORT --baud 921600 write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 devday-terminal-factory-1.0.0.bin
```

Then verify against `release/SHA256SUMS.txt` and run serial `factory.check`
on each unit (see docs/LINE_TEST.md).

## Flash map

| Offset | Region | Size | Purpose |
|---|---|---|---|
| `0x0` | bootloader | 0x7000 | 2nd-stage bootloader |
| `0x8000` | partition table | 0x1000 | `partitions.csv` |
| `0x9000` | nvs | 0x6000 | `Preferences` config |
| `0xf000` | phy_init | 0x1000 | RF calibration |
| `0x10000` | factory | 3 MB | ships the RC |
| `0x310000` | ota_0 | 3 MB | signed updates |
| `0x610000` | ota_1 | 3 MB | signed updates |
| `0x910000` | otadata | 0x2000 | OTA boot selection |
| `0x912000` | nvs_keys | 0x1000 | NVS keys |
| `0x920000` | littlefs | 6.9 MB | content cache |

## Signed updates

The on-demand portal (`POST /update`) accepts only application-only images
signed with the production RSA-2048 key (RSASSA-PKCS1-v1_5 over SHA-256,
264-byte trailer — see `tools/sign_update.sh`). Corrupted or wrong-key
binaries are rejected; an interrupted upload never touches the running slot,
so the previous application keeps booting.

Sign with the production private key from 1Password (never committed):

```sh
tools/sign_update.sh app.bin app.signed.bin /path/to/prod_update_key.pem
```

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
esptool.py --chip esp32s3 --port PORT write_flash 0x10000 devday-terminal-recovery-1.0.0.bin
```

3. If configuration is suspect, hold **D1+D4** at boot to factory-reset, or
   send `factory_reset` over USB.
