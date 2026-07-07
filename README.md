# Splaton't

A very unofficial ink-em-up: a 2D top-down multiplayer shooter in the spirit of
Splatoon, with CS2D-style presentation and a pixel-art look. Written in C++17 on
a small custom framework (raylib for window/render/audio, ENet for UDP netcode,
SQLite for the local account database). Everything — sprites, tiles, sounds — is
generated procedurally at runtime; there are no asset files.

## Features

- **Client/server multiplayer** — authoritative dedicated server over reliable
  UDP (ENet), snapshot interpolation for remote players, client-side prediction
  for your own movement, live ping readout. The server runs up to **3 matches
  concurrently**, so queueing for one mode never blocks the other. Works over
  LAN; localhost out of the box.
- **Accounts** — register/login with salted-SHA256 passwords, stored in a local
  SQLite database (`splatont.db`, created next to the server exe). Lifetime
  stats (splats, deaths, W/L, turf inked) persist per account.
- **Loadouts — six weapons**, balanced around distinct roles and weight classes
  (heavier weapons run slower):
  - *Splattershot* — the all-rounder baseline (3-shot splat)
  - *Aerospray* — fastest fire and best paint output, weakest duels
  - *Blaster* — slow explosive shells; direct hits one-shot, splash chips
  - *Ink Roller* — paints a swath as you hold fire; lethal only point-blank
  - *Splat Charger* — charge and release; one-shot snipes, pierces at full charge
  - *Splatling* — rev up, then unleash a long sustained barrage
  plus **two sub weapons** (RMB/Q): *Splat Bomb* (lobbed, delayed, lethal) and
  *Burst Bomb* (cheap, pops on impact), and four ink styles.
- **Three maps** — Dockside, Warehouse, Plaza — all mirror-symmetric, rotating
  between matches.
- **Special weapons** — painting turf (and splatting) charges your special
  meter; press **F** to unleash it: **Inkzooka** (piercing long-range blasts),
  **Ink Storm** (a thrown raincloud that paints and chips enemies), or
  **Bubbler** (brief invulnerability). Which special you get depends on your
  main weapon.
- **Progression** — every match pays out coins (kills, turf inked, win bonus);
  spend them on hats (Cap, Cone, Phones, Crown) that render on your character.
- **Two modes**
  - **Turf War (4v4)** — 3 minutes; the team that paints more floor wins.
  - **Team Deathmatch** — first team to 25 splats (or best in 4 minutes).
- **Splatoon mechanics** — paint the ground, swim in your own ink (SHIFT) to
  move fast and refill your tank, get slowed in enemy ink, respawn on your pad
  with brief spawn protection (a shield that pops when you fire).
- **Match UX** — 3-2-1-GO intro, TAB scoreboard, death cam that follows your
  killer, kill feed, quick chat (C/X/V/B) with speech bubbles, ESC pause menu.
- **Bots** — matches always fill to 4v4 with pathfinding bots that fight, paint
  objectives, throw bombs, pop specials, and occasionally taunt; humans can
  join a running match and take over a bot slot. Leavers are replaced by bots.
- **Display & audio options** — 1x/2x/3x window sizes and borderless fullscreen
  with crisp integer pixel scaling, music/SFX volume sliders (procedural
  chiptune soundtrack), FPS display (Settings, saved to `settings.txt`).

## Building (Windows)

Requirements: Visual Studio 2022 (C++ workload), CMake 3.24+, Ninja, internet
access on first configure (dependencies are fetched by CMake).

```
scripts\build.bat
```

Produces `build\splatont.exe` (client) and `build\splatont_server.exe` (server).

## Running

Easiest: run `build\splatont.exe` and click **HOST + PLAY** — it starts a local
server and connects to it. Or run the pieces yourself:

```
build\splatont_server.exe        # dedicated server on UDP 27777 (port = argv[1])
build\splatont.exe               # client; enter the server IP (127.0.0.1)
```

Multiple clients on one machine work fine (each needs its own account). Other
PCs on your LAN can connect to your IP; the backend is local-only for now.

### Hosting beyond your LAN

The server was hardened for remote play and is ready to be exposed when you
want to try it:

- Passwords are stored with **PBKDF2-HMAC-SHA256** (60k iterations, salted).
  Old sha256 hashes from earlier versions upgrade automatically on login.
- Clients get a **session token** on login and resume automatically after a
  disconnect without resending the password.
- Failed logins are **rate-limited per IP** (5 tries / minute).
- `server.cfg` (created next to the exe on first run) controls the port, match
  lengths, TDM kill target, and lobby countdown without recompiling.

To host: run `splatont_server.exe` on the host machine, forward **UDP 27777**
(or your configured port) on the router, then click **COPY MY JOIN CODE** on
the client's title screen and send friends the code (e.g. `SQ-4RA4W-7V80P`).
They paste it into the address box. Codes mask your IP from casual eyes but
are decodable by anyone holding one — for true IP privacy, tunnel with
**playit.gg** (free, UDP, no port forwarding) or **Tailscale** and share that
hostname instead; the address box accepts hostnames and `host:port`.

Hit registration is lag-compensated (targets rewound by the shooter's
latency), so play over the internet feels fair up to moderate ping. The
remaining hardening item for serious public hosting is fully
server-authoritative movement (currently client-predicted and clamped).

### Controls

| Input | Action |
|---|---|
| WASD / arrows | move |
| Mouse | aim |
| LMB | shoot (hold to charge the Charger/Splatling, hold to roll with the Roller) |
| RMB / Q | throw your sub weapon (costs ink — see the notch on the ink tank) |
| F / MMB | activate your special when the meter is full |
| SHIFT | squid form — swim fast in your ink, refills tank, can't shoot |
| TAB | scoreboard |
| C / X / V / B | quick chat: Booyah! / This way! / Help! / Nice! |
| ESC | pause menu (resume · settings · leave match) |

### Headless smoke test

The client has a built-in autopilot used for testing:

```
splatont.exe --auto <name> <password> <mode 0=turf|1=tdm|9=loadout-screen> <seconds>
```

It registers/logs in, queues, plays with synthetic input, saves periodic
screenshots to the working directory, and prints an `AUTOTEST:` summary.

## Layout

```
src/shared/   protocol + deterministic sim shared by client and server
              (map, paint grid, movement/collision)
src/engine/   framework pieces: ENet wrapper, SHA-256
src/server/   dedicated server: SQLite accounts, lobby, match sim, bot AI
src/client/   raylib client: screens, procedural pixel art + sounds, HUD
```

Notes on the architecture: the server simulates at 30 Hz and broadcasts
snapshots + reliable paint-cell deltas; clients send position/aim/buttons at
30 Hz (movement is client-predicted and server-clamped; shooting, damage,
painting and scoring are fully server-side). The paint surface is a 192x128
cell grid (4px cells) over a 48x32 tile map.
