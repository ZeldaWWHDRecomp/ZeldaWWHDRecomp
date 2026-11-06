# Online server (`wwhd-online-server`)

The online server lets players see each other at sea and in the same places, and lets parties of
2–4 friends play together. It is open source and self-hostable. There is no official server: you
run your own, on your computer ("Host on this computer" in the game) or on a box you control.

> **Status: experimental.** Traffic is encrypted and authenticated with the vendored
> [Monocypher](https://monocypher.org) 4.0.2: X25519 key exchange, XChaCha20-Poly1305 packets and EdDSA
> identities. Players pin the server's key the first time they connect, and are warned if it changes.

## What the server does and doesn't do

- It relays poses, emotes and party messages between players. It never runs game logic and ships
  no game data.
- Players only ever see the server, never each other's IP addresses.
- **What it keeps on disk:**
  - its key (`server.key`), the ban list and the report log;
  - the parties' last known team state (`team-state.txt`, on by default): the party members' key hashes and
    their shared game progress, so a party survives a restart. A party nobody has played in for
    `party.team_state_keep_h` hours (72) is dropped. Set `files.team_state = ""` to keep it in memory only.

  Nothing else is kept: IP addresses stay in memory for the session.
- **Compatibility:** players need the same protocol version and the same game data. Different game
  versions or regions never see each other, even on one server.

## Running it

### Build

```sh
cmake -S server -B build-server -DCMAKE_BUILD_TYPE=Release
cmake --build build-server -j 4
build-server/wwhd-online-server --config server.toml
```

The server needs only `server/` and `runtime/src/online/proto/`. From the port's own build, the
`WWHD_ONLINE=ON` option builds it too.

### Configure

Copy `server/server.example.toml` to `server.toml` and edit it. The keys you'll usually change:

| Key | Default | Meaning |
|---|---|---|
| `server.name`, `server.contact`, `server.rules` | | Shown to players before they connect. The contact is your name or address, for the privacy notice. |
| `server.public` | off | A public server (in a community list). Needs the real crypto backend, which is the default. |
| `server.invite_only`, `server.allow` | off | Only the listed friend codes may join. |
| `network.bind`, `network.port` | `127.0.0.1`, `7777` | Where it listens (UDP). Use `0.0.0.0` to accept players from other machines. |
| `network.max_players` | 1000 | |
| `network.game_data` | any | Only these game-data hashes may join. |
| `world.copy_size` | 64 | Players per copy of an interior (a new copy opens when one is full). |
| `scale.area_cap` | 200 | Players per small area of sea and copy before newcomers go to a copy. |
| `limits.pow_bits` | 18 | The join puzzle (about 0.3 s on a phone). Higher slows down mass sign-ups. |
| `limits.per_ip` | 4 | Players from one address. |
| `party.max_size` | 8 | The largest party, between 2 and 16. Players see it before they join. |
| `files.team_state` | `team-state.txt` | Parties and their team progress over restarts (`""` keeps them in memory only). |
| `metrics.file` | none | Writes a line of counts (players, traffic, tick times) every `every_s` seconds. |

All other keys (interest radii and caps, rate limits, scaling thresholds) have working defaults and
are listed in `server/src/config.h`.

### Mod rules

Players may use mods. Look-only mods (textures, models, UI, graphics packs) are never checked. Gameplay
mods are mods whose manifest says they change gameplay; each has an id, a version and a hash of its
content. A game tells the server which gameplay mods it has enabled, and the server decides who may
join:

| Key in `[mods]` | Meaning |
|---|---|
| `vanilla_only = true` | No gameplay mods at all. |
| `required = [...]` | Players must have these. |
| `allowed = [...]` | If given, only these (and the required ones) may be used. |
| `banned = [...]` | These may not be used. |

Each entry is `"id"`, `"id@version"` or `"id@version#<64 hex digits>"`. `"*"` as the version means any
version, and the hex is the content hash. With no `[mods]` section there are no rules, and anything
goes.

- **Before joining.** Players see a server's rules in the server list before they join.
- **Refusals.** A refused player is told exactly which mods are missing, not allowed, banned or the
  wrong version.
- **Parties.** A party plays with one set of gameplay mods: its leader's. Someone with a different set
  can't join that party; the game names the differences.
- **No downloads.** A server never sends mods, code or content of any kind. It only states its rules.

Example: a randomizer-only server. Everyone plays the same randomized seed, with the hash standing for
seed + settings, and no other gameplay mods:

```toml
[mods]
required = ["randomizer@*#0f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f0"]
allowed = ["randomizer"]
```

The `allowed` list makes it a whitelist, so any other gameplay mod is refused. Add more ids to it to
permit a few more.

Example: a vanilla-only server:

```toml
[mods]
vanilla_only = true
```

### PvP at sea

Players can fight each other on the open sea; the server referees.

```toml
[pvp]
sea = "opt-in"              # "off", "opt-in" (each player's toggle, on by default) or "on" (everyone, no toggle)
zones = ["10", "12:r=20000", "3:x=500,z=-200,r=15000", "-16"]   # safe zones, see below
protect_s = 12              # protection after leaving a safe zone and after a knockout
enter_delay_s = 10          # fleeing into a safe zone mid-fight: safe only after this long
hide_band = 8000            # PvP players this far outside a safe zone are hidden from strangers' charts
ko_rupee_percent = 10       # a knockout costs this share of the wallet (0..50); the rupees vanish
ko_rupee_max = 100
max_hit_dist = 3000
cooldown_kos = 2            # after this many knockouts of one player by the same attacker ...
cooldown_window_s = 180     # ... within this window,
cooldown_s = 300            # ... that attacker can't hit that player, or cost them rupees, for this long
```

- **Safe zones** are circles in sea squares. Squares are numbered row by row from the north-west
  corner, 0 to 48.
  - Entry forms:
    - `"10"`: the square's centre, radius 15,000;
    - `"12:r=20000"`: another radius;
    - `"3:x=500,z=-200,r=15000"`: a centre offset from the square's centre;
    - `"-16"`: removes square 16's zone;
    - `zones_replace = true`: starts from an empty list.
  - The default list is squares 0, 3, 10, 12, 16, 25, 40, 43 and 44, the big islands, centred. It is
    a first approximation until it is measured.
  - Every place other than the open sea (interiors, dungeons, their entrances) is always safe.
- **What the server checks** for every hit:
  - both players have PvP on;
  - neither is in a safe zone or protected (busy in a cutscene, shop or menu counts as protected);
  - the distance and the hit rate are plausible;
  - neither has blocked the other;
  - not in one party, unless the party's friendly fire is on (then zones and toggles don't matter and
    a knockout costs no rupees);
  - not inside the repeat-knockout cooldown.

  Anything else is dropped and counted in the metrics. Attacking ends one's own protection.
- **A knockout** costs the knocked-out player a share of their rupees. The rupees vanish; the attacker
  gains nothing. The player comes back at the nearest safe zone's edge, protected for a while.

### Block and mute

A player can block or mute another player from the game:
- **Block:** both players disappear for each other: poses, emotes, effects and messages, party members
  included.
- **Mute:** only stops the other player's emotes, effects and sounds.

The server keeps both lists per player, across reconnects and between server processes. The metrics
count the traffic they stop.

### Ports and networks

- One port (default 7777) for the players, UDP and TCP. Forward both on your router if friends join
  over the internet.
  - UDP is the normal way.
  - TCP (`network.tcp = true`, the default) is the fallback for networks that block UDP. A game that
    gets no answer over UDP for 3 s tries TCP at the same address, and shows "(TCP)" in its status.
  - TCP carries the same encrypted packets. On a congested connection it holds back other players'
    poses rather than delaying everything.
- With `scale.mode = "auto"` the server may start worker processes; they listen on
  `scale.worker_port_base` and up (default 7778, 7779, …). Forward those too, or set
  `scale.max_workers = 1`.
- Admin commands use a local socket file (`wwhd-online-admin.sock`), never the network.

### Messages a removed player sees

A player who stays over a limit is first warned, then removed with a reason code and the limit in the text
(for example "upload over this server's limit of 8192 bytes/s"). The limits are in `[limits]`.

### Admin commands

```sh
wwhd-online-server admin status
wwhd-online-server admin players
wwhd-online-server admin kick KQ7F-9MZ2-XD "reason"
wwhd-online-server admin ban KQ7F-9MZ2-XD 7d "reason"     # 30m, 12h, 7d or perm
wwhd-online-server admin unban KQ7F-9MZ2-XD
wwhd-online-server admin bans
wwhd-online-server admin notice "text for everyone"
wwhd-online-server admin restart 30                         # a 30 s notice, then the server stops
wwhd-online-server admin parties
wwhd-online-server admin regions                            # scaling: workers, regions, recent changes
```

Players are named by friend code. A ban holds across reconnects and restarts; with
`limits.keep_ip_hash = true` it also keeps a salted hash of the player's IP address.

### Restarts

After `admin restart` (or a crash), start the server again. Players' games keep running, show
"reconnecting" and come back by themselves. Under Docker or systemd, a restart policy does this for you.

Parties survive restarts in two ways:
- **From disk.** The saved team state, written within 2 s of any change, brings each party back with
  its progress, and members are put back in it as they sign in.
- **From the games.** Each game re-sends the record its party leader signed. The party is rebuilt from
  the leader's claim, or from a member's signed record, and the members re-send their progress.

### Docker

```sh
server/docker-build.sh                 # builds the image "wwhd-online-server" (~5 MB, from scratch)
docker run -d --restart unless-stopped -p 7777:7777/udp -v $PWD/data:/data wwhd-online-server
```

Put `server.toml` in `data/` with `bind = "0.0.0.0"`. The key, bans and reports are kept there too.

## Scaling: from a few friends to thousands

One process is enough for small servers and is the default (`scale.mode = "off"`).

With `scale.mode = "auto"`, the server scales by itself on one machine:

- **Regions.** The sea is a map of regions, at first one per square. Each region is run by one
  worker process. The first process is the coordinator and also a worker.
- **Measuring.** Every second, each worker reports its CPU, its outgoing traffic and how long its
  tick takes. A worker over `scale.split_cpu`, `scale.split_bw` or its tick budget for
  `scale.hold_s` seconds sheds load:
  - a busy region moves to a less busy worker;
  - a region too busy on its own splits in two where its players are, down to single sub-cells
    (12,500 units);
  - when no worker has room, the coordinator starts another worker process (up to
    `scale.max_workers`);
  - when even the smallest region is too full, newcomers go into copies of that area, and those
    copies can run on other workers. Parties always stay together, and a player can join a
    friend's copy.
- **Calming down.** Split regions merge back, regions return to the coordinator, and started workers
  stop when idle.
- **Seamless.** Players near a region border still see each other: each worker receives the
  players in the cells its own players look into. A player who sails into another worker's region
  is handed over and keeps their id; the switch takes about 0.15 s and nobody disappears from view.

**Threads.** Each process runs its tick on several threads:
- `interest.threads` sets them for this process (default: half the cores, at most 16);
- `scale.worker_threads` sets them for the workers it starts (default: the cores shared by
  `scale.max_workers`, between 2 and 16).

On a big machine, either raise `max_workers` (more processes) or the threads per process.

Several machines: run the coordinator with `scale.mode = "pool"` (or `"auto"`) and
`scale.link_bind` set to a LAN address. On each extra machine, run:

```sh
WWHD_ONLINE_LINK_SECRET=... wwhd-online-server --worker --join <coordinator LAN address>:7900 \
    --config server.toml --public-host <this machine's public name>
```

Copy the coordinator's `server.key` to every worker so that players see one server key. Workers
register by themselves and the coordinator uses them when it needs to.

### Measured (one MacBook, everything on it at once)

Bots are headless clients that send a 140-byte pose 15 times a second, with phone-sized view caps.

| Test | Result |
|---|---|
| 1,000 players spread over the sea (one process) | 11 KB/s down and 2.5 KB/s up per player; tick p50 8 ms |
| 1,000 players in one square (auto) | one worker started, the square split in two; tick p50 11 ms; 19 KB/s per player |
| 5,000 players in one square (auto) | 6 workers, 9 splits and one copy moved; all 5,000 stayed online; handover p50 132 ms, p99 0.66 s; ping p50 83 ms |
| 2,000 players sailing into one square within a minute | 1,320 handovers, all players stayed online, 9 brief losses of sight; handover p50 132 ms; ping p50 10 ms |
| The 1,000-player crowd leaves, then 150 s of calm (auto) | the split halves merged back 20 s after the last player left, and the started worker stopped 30 s later |
| Pool mode, two simulated machines (two worker processes each, own folders and ports), 1,200 players in one square, one machine killed after 100 s | splits on both machines, handovers between them (p50 134 ms); the lost machine's regions went to the other machine and the coordinator; 1,197 of 1,200 online at the end, the moved players back within about 6 s |
| 2,000 players in one square for 150 s, then 30 min with nobody (auto) | 3 workers started, 6 splits and one move; 45 s after the last player left every split had merged back, 30 s later the 3 workers stopped; no worker started again in the rest of the 30 min |

**Real capacity needs a Linux server, with the bots on another machine.** These numbers come from one
Mac where the bots and every server process share the same cores. Linux also sends UDP in batches
(`sendmmsg`), which macOS can't. On one Mac the 5,000-player crowd is limited by the machine itself: the bots, all the workers and the
sending (macOS has no batched UDP send) share 16 cores, and the workers' ticks reached 55–110 ms
instead of 66 ms. A Linux server uses batched sends (`sendmmsg`), and a second machine in pool mode
adds cores.

## For developers

### Layout

- `runtime/src/online/proto/`: the protocol library shared by the game and the server. It covers
  packets, reliable messages, the handshake, the client (`proto/client.h`), friend codes and
  message codecs.
- `server/src/`:
  - `server.cpp`: handshake, sessions, interest, copies;
  - `party.cpp`: parties, runners, team state;
  - `scale.cpp`: regions, links, the coordinator;
  - `admin.cpp`: admin commands and bans;
  - `config.cpp`: the config file.
- `server/bots/`: the load-test bots.
- `server/tests/`: unit and end-to-end tests and the fuzz target.

### Wire format (summary)

- **Datagrams** are at most 1,200 bytes. A data packet has a 17-byte header (kind, connection id,
  packet number, newest packet seen, 32 earlier ones as bits), then a sealed body and a 16-byte tag.
- **Body.** The body has the sender's clock, an echo of the peer's clock (for the ping), then frames:
  - reliable ordered frames are resent until acked; longer ones go in 1,000-byte pieces, up to 64 KB;
  - unreliable frames are sent once.
- **Handshake:**
  1. `CONNECT` (padded to a full datagram);
  2. `CHALLENGE` (a proof-of-work salt, the server's key and info, signed);
  3. `HELLO` (protocol, game-data hash, identity key, session key, proof-of-work, name, resume token;
     signed);
  4. `WELCOME` (connection id and the player's id; sealed).

  A resume token or a handover ticket skips the proof-of-work.
- **Presence.** Poses carry a fixed header that the server reads (place, position, heading,
  animation) plus the game's own pose bytes, which the server passes on unread. The server sends
  them back in tiers:
  - near: everything, 15 Hz;
  - mid: 20 bytes, about 4 Hz;
  - far: 11 bytes, 1 Hz;
  - chart markers: 6 bytes, 0.2 Hz.
- **Parties.** The game's own messages travel as opaque app messages to the party, one member, the
  enemy runner of the place, the players in the place, or the near players. The server picks each
  place's runner (the first member there) and names a new one at once when that member leaves, or
  after 3 s of silence. The team progress store merges fields as a union:
  - bits OR'ed;
  - counters MAX'ed;
  - slots filled only where empty.

### Crypto backends

`proto/crypto.h` is the only interface. CMake `WWHD_ONLINE_CRYPTO` picks the backend:
- **`monocypher`** (the default): the vendored Monocypher 4.0.2 in
  `runtime/src/online/proto/third_party/monocypher` (BSD-2-Clause or CC0, unchanged, with its licence and
  SHA-256s in its README).
  - **Identities and the server key:** EdDSA (Curve25519, BLAKE2b). The identity file keeps the 32-byte
    seed.
  - **Sessions:** X25519 between fresh ephemeral keys, then keyed BLAKE2b into one key per direction.
    All-zero shared secrets are refused.
  - **Packets:** XChaCha20-Poly1305, with the packet number and direction as the nonce and the header as
    additional data. A replayed or altered packet is dropped.
  - **Signatures:** the client signs its HELLO with its identity key, and the server signs its CHALLENGE
    and WELCOME with the server key, which the client pins. Handover tickets are random 32-byte tokens.
    They travel between workers over the encrypted link and reach the client inside its encrypted
    session. The new worker accepts a ticket only together with a HELLO signed by the same identity
    key.
- **`plain`**: the same message shapes with no secrecy and no authentication, for protocol debugging
  only. Both sides must use the same backend.

### Tests

```sh
build-server/wwhd_online_proto_test           # codecs, friend codes, PoW, the reliable layer under loss
build-server/wwhd_online_server_test .        # end to end over 127.0.0.1, incl. scaling and bad networks
build-server/wwhd_online_fuzz 200000          # random and mutated packets (or -DWWHD_ONLINE_FUZZ=ON with libFuzzer)
cmake -S server -B build-asan -DWWHD_ONLINE_SANITIZE=ON   # ASan + UBSan
build-server/wwhd-online-bots --port 7777 --count 1000 --scenario mixed --duration 60 --report out.json
```
