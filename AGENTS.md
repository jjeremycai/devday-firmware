# Dev Day Terminal — agent instructions

The device screen tells its owner to ask you two things: **"set up my Dev Day
terminal"** (splash, Usage and Weather pages) and **"put my calendar on my
terminal"** (Agenda page). This file is how you answer both.

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

Three pages on the first three keys: Usage, Weather, Agenda.

**To put anything on the screen, write a schema-1 document and push it:**

```sh
tools/push.py agenda.json                  # or: … | tools/push.py -
tools/push.py --show agenda agenda.json    # and switch to that page
```

```json
{
  "schema": 1,
  "agenda": {
    "date": "Thursday, August 6",
    "events": [
      {"time": "09:30", "title": "Standup", "detail": "with design · Room A"}
    ]
  }
}
```

Documents merge section by section, so a push containing only `agenda` leaves
the owner's pet and usage untouched. Four events max, and the whole document
must stay under 12 KB — `push.py` checks both before sending. Full schema in
`docs/PROTOCOL.md`.

**For their real calendar** — the answer to *"put my calendar on my
terminal"* — on macOS just run the sync: it reads the local calendar store
automatically (`tools/localcal.py`, stdlib sqlite, no install). If macOS
denies the read, or on Linux, point it at a feed instead:

```sh
tools/dash_sync.py --ics "<their calendar's private iCal URL>"
```

Google Calendar exposes this as *"Secret address in iCal format"* under a
calendar's settings; iCloud as a public calendar link. Do not reach for
AppleScript — enumeration takes minutes on a large calendar.

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
- `firmware/pet_asset.h` is **generated**. Edit the artwork and rerun
  `tools/gen_pet.py`; do not hand-edit the header. The first-boot splash is
  pure geometry in `cards.cpp` — no asset to regenerate.
- `tools/imaging.py` holds the one implementation of image conversion. Sprite
  art gets an alpha silhouette, photographs get Floyd-Steinberg. Do not add a
  fourth copy of a dithering loop.
- The preview harness compiles the real `cards.cpp`, so a render is honest.
  Check a layout change there before flashing.
