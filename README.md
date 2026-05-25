# Claude Code LED Indicator for Arduino UNO Q

An animated Claude Code status indicator that runs on the
[Arduino UNO Q](https://docs.arduino.cc/hardware/uno-q)'s 8×13 LED matrix.
Claude Code lifecycle events on your Mac are pushed over Wi-Fi to the board,
and the cube-bodied Claude character (from the welcome screen) reacts in
real time — walking while it thinks, jumping when it's done, looking
confused on errors.

```
.................
..■■■■■■■■■......     ← head
..■■.▒▒▒.■■......     ← head with eyes
■■■▒▒▒▒▒▒▒■■■....     ← shoulders
.■▒▒▒▒▒▒▒▒▒■.....     ← body
..■.■...■.■......     ← legs
.................
```

## Live viewer

**▶ Open: <https://hulryung.github.io/claude-code-unoq-indicator/viewer/>**

[`viewer/index.html`](viewer/index.html) is a self-contained HTML page that
renders the matrix in your browser using the same sprite data and animation
logic as the MCU sketch. Three modes:

- **Local sim** — runs the animation entirely in JS, no board needed.
- **Mirror live board** — polls `http://Janie.local:8765/state` and shows
  what the physical LED matrix is showing right now.
- **Push to board** — buttons send `POST /state` so you can drive the
  board from the page.

Just open the file in any browser:

```bash
open viewer/index.html
```

The board's HTTP server sends `Access-Control-Allow-Origin: *`, so the
mirror and push modes work from a local `file://` URL without proxies.
The hosted GitHub Pages URL is HTTPS, so mixed-content rules block the
HTTP board calls — open the local file (`open viewer/index.html`) when
you want the mirror/push modes against a LAN board.

## Architecture

```
Mac terminal                                    Arduino UNO Q (Janie)
─────────────                                  ─────────────────────────────
Claude Code                                     wlan0  192.168.50.18
   │
   ├─ UserPromptSubmit hook ─┐
   ├─ PreToolUse hook       │
   ├─ PostToolUse hook       ├─→ ~/bin/unoq-state <state>
   ├─ Stop hook              │     │
   ├─ Notification hook      │     │  curl -X POST -m 1
   └─ SessionStart hook ─────┘     ▼
                               http://Janie.local:8765/state
                                          │
                                          ▼
                                    MPU (Debian 13 aarch64, Python in Docker)
                                          │  Bridge.call("set_state", ...)
                                          ▼
                                    MCU (STM32U585)
                                          │
                                          ▼
                                    8×13 LED matrix, 3-bit grayscale, ~50 FPS
```

Six visual states map onto Claude Code lifecycle events:

| State | Trigger | Animation |
|-------|---------|-----------|
| `idle` | SessionStart, fallback | Character standing, slow breathing pulse |
| `thinking` | UserPromptSubmit, PostToolUse | Claude Code 6-frame ASCII spinner cycling (`·` `✢` `✳` `∗` `✻` `✽`) |
| `tool` | PreToolUse | Fast walking + shoulder ticks (arms swinging) |
| `done` | Stop | Arms-up celebration jump, then fades to idle |
| `notify` | Notification | Soft attention pulse |
| `error` | (manual) | Question mark above head, body wobble |

## Prerequisites

- Arduino UNO Q on the same Wi-Fi as your Mac
- macOS with [Claude Code](https://code.claude.com) installed
- SSH/`arduino-app-cli` access to the board (see [why the official App Lab is bypassed](#why-the-official-app-lab-is-bypassed))
- mDNS reachable (`Janie.local` or whatever hostname you set) or fixed IP

## Setup

### 1. Deploy the app on the board

```bash
# SSH to your UNO Q
ssh arduino@<board-ip>

# Pull this repo on the board (or scp the claude-anim/ folder)
cd ~/ArduinoApps
git clone https://github.com/<you>/<repo>.git tmp && mv tmp/claude-anim . && rm -rf tmp

# Build, flash MCU, start MPU container
arduino-app-cli app start user:claude-anim
```

Verify:
```bash
curl http://<board-ip>:8765/health
# {"ok": true, "uptime": 12}
```

### 2. Install the helper on your Mac

```bash
mkdir -p ~/bin
cp scripts/unoq-state ~/bin/
chmod +x ~/bin/unoq-state
# Test it
~/bin/unoq-state thinking
~/bin/unoq-state idle
```

By default it hits `http://Janie.local:8765/state`. Override with
`UNOQ_URL=http://192.168.x.y:8765/state` if your board has a different
hostname or you want to bypass mDNS.

### 3. Wire up Claude Code hooks

Add the contents of `claude-hooks.example.json` into your
`~/.claude/settings.json` `"hooks"` key (or merge it with `jq`):

```bash
jq -s '.[0] * .[1]' ~/.claude/settings.json claude-hooks.example.json \
    > /tmp/settings.new && mv /tmp/settings.new ~/.claude/settings.json
```

Restart Claude Code. The character should start reacting to your prompts.

## Customization

### Change the character's look

Edit `claude-anim/sketch/sketch.ino` — the sprites (`BODY`, `SPINNER`,
`CELEBRATE`, `CONFUSED`) are 2-D arrays of brightness values 0..7.
Rebuild and flash:

```bash
arduino-app-cli app stop user:claude-anim
arduino-app-cli app start user:claude-anim
```

### Add a new state

1. Add an `ST_<NAME>` enum value in `sketch.ino`
2. Implement `render<Name>(unsigned long now)` and add a `case` to `loop()`
3. Map the string in `set_state()`
4. (Optional) Add a hook entry pointing to `~/bin/unoq-state <name>`

### Demo loop

```bash
~/bin/unoq-demo-loop   # cycles all 6 states, 5s each, repeat
~/bin/unoq-demo-stop   # back to idle
```

## Why the official App Lab is bypassed

Arduino App Lab 0.8.0/1.0.0 has a Wails native-layer sandbox bug on
macOS Apple Silicon ([arduino-app-lab #17](https://github.com/arduino/arduino-app-lab/issues/17))
that traps the first-run setup wizard. The board itself is fine — the
Mac GUI just never advances past keyboard layout / "Looking for updates."
The repo uses CLI-only deployment (`arduino-app-cli` over SSH) to avoid
the broken UI entirely.

## Files

```
.
├── claude-anim/
│   ├── app.yaml             # arduino-app-cli app descriptor (port 8765)
│   ├── python/main.py       # HTTP server bridging Mac → MCU
│   └── sketch/sketch.ino    # MCU animations on the LED matrix
├── scripts/
│   ├── unoq-state           # CLI: push a state to the board (fire-and-forget)
│   ├── unoq-demo-loop       # cycle through all states forever
│   └── unoq-demo-stop       # stop the demo loop
├── viewer/
│   └── index.html           # browser-side LED matrix viewer (local + live mirror)
├── claude-hooks.example.json # snippet to merge into ~/.claude/settings.json
└── README.md
```

## License

MIT
