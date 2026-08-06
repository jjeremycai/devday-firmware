# Dev Day Terminal — agent instructions

The device screen tells its owner to ask you: **"set up my Dev Day terminal"**.
This file is how you answer that.

## Setting up the terminal

The terminal is an e-paper display on USB. It boots useful with no network, but
it shows real data — the owner's Codex pet, token usage, and local weather —
only once the sync service is installed. That is a single command:

```sh
tools/dash_sync.py --install
```

That registers a per-user LaunchAgent (macOS) or systemd user service (Linux),
so every later plug-in re-syncs on its own. It needs no root, no new Codex
authorization, and no browser permission: it reuses the Codex login already on
disk.

Then confirm it worked:

```sh
tools/dash_sync.py            # one-shot push, prints what it found
```

The screen should show the owner's pet, their usage numbers, and the weather
within a few seconds. If the terminal is not plugged in, `--watch` waits for it.

### If the repo is not on their machine

```sh
git clone https://github.com/jjeremycai/devday-firmware
cd devday-firmware && tools/dash_sync.py --install
```

### Troubleshooting

- **No serial port found** — the terminal must be plugged in over USB, and the
  cable must carry data. Some charge-only cables do not.
- **"pet skipped"** — no pet was resolvable offline and the download failed.
  `tools/dash_sync.py --pet codex` forces the built-in one.
- **Usage is empty but the pet appears** — `codex app-server` could not be
  reached; check that the Codex CLI is installed and signed in.
- **Nothing on screen changes** — press KEY1. The terminal keeps showing
  whichever page you left it on.

## Changing what it shows

Three pages on the first three keys: Usage, Weather, Agenda. Content arrives as
one JSON document over USB (`content.push`) — see `docs/PROTOCOL.md` for the
schema, and `tools/dash_sync.py` for a working sender.

The pet follows `tui.pet` in `~/.codex/config.toml` — whichever one the owner
picked in Codex. To override it for the terminal only:

```sh
tools/dash_sync.py --pet dewey        # a built-in
tools/dash_sync.py --pet ~/.codex/pets/mypet
```

Built-ins: `codex`, `dewey`, `fireball`, `rocky`, `seedy`, `stacky`, `bsod`,
`null-signal`. With `tui.pet = "none"` the terminal shows the profile photo.

## Rebuilding the firmware

```sh
tools/build.sh                # compile
tools/preview/build.sh        # render every page to PNG, no device needed
```

Flashing is unrestricted over USB — this is the owner's device to reprogram.
See `README.md` for the pinned toolchain and `docs/FLASHING.md`.

## Working in this repo

- `firmware/` is the whole Arduino sketch. `cards.cpp` draws every page.
- `firmware/pet_asset.h` and `firmware/devday_splash.h` are **generated**. Edit
  the artwork and rerun `tools/gen_pet.py` / `tools/gen_splash.py`; do not hand-
  edit the headers.
- `tools/imaging.py` holds the one implementation of image conversion. Sprite
  art gets an alpha silhouette, photographs get Floyd-Steinberg. Do not add a
  fourth copy of a dithering loop.
- The preview harness compiles the real `cards.cpp`, so a render is honest.
  Check a layout change there before flashing.
