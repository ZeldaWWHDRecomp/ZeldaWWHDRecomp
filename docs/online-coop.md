# Online co-op (experimental)

Play *The Wind Waker HD* together with up to three friends. Each player runs their own copy of this
port with their own copy of the game. One player hosts, the others join.

The online co-op code comes from [Galvakron's ZeldaWWHDRecompMP](https://github.com/Galvakron/ZeldaWWHDRecompMP)
(MPL-2.0), co-op work by Galvakron and AsadSi.

## Before you start: privacy

Online co-op connects your game directly to the other players' games. **They see your IP address,
and you see theirs.** An IP address can reveal your approximate location. Only play with people you
trust.

Nothing is sent over the network until you switch online co-op on and accept this notice. It stops
when you switch it off.

## Your save

- Shared progress is written into **your own Quest Log** when you save, as in the other players'
  games. Nothing is ever taken away: a flag or item one player has is added for the others, never
  cleared.
- Before your first online session the game makes a **backup** of your save in the `online-backups`
  folder of your user folder.
- We recommend a **new Quest Log just for co-op**.

## Who can play together

- Everyone needs a version of the port with the same online co-op version, and the **same game**:
  the same region and version (and, for randomized games, the same seed).
- The normal build and the decomp build can play together.
- A player with another game or another co-op version is refused with a message saying why.

## How to play (this development version)

There is no Settings page for online co-op yet. In this version it is switched on when the game is
started:

- **Host:** `WWHD_COOP=host` (UDP port 7777; another port: `WWHD_COOP=host:7800`).
- **Join:** `WWHD_COOP=join:<the host's address>` (an IP address or a name, e.g. a VPN name;
  another port: `join:<address>:7800`).
- `WWHD_COOP_NOTICE_OK=1`: you accept the privacy notice above. Without it nothing is sent.
- `WWHD_COOP_NAME=<your name>`, `WWHD_COOP_TUNIC=0..3` (green, red, blue, purple).

The host's UDP port must be reachable from the other players. On a VPN such as Tailscale nothing else
is needed.

## What is shared

- **The other players** are shown as Link in their own tunic, with their animations, sword and items,
  in the same room as you. You hear their sword, steps and voice where they are. Players push each
  other.
- **Their items:** you see what they hold, throw and fire. Their bombs go off in your game too.
- **The story and items:** story events, chests, switches, pickups, small keys and dungeon items,
  sunken treasure, items, songs, pearls, the Triforce, charts and maps, heart containers and bag
  sizes. A chest or door another player opens in a room you are in opens for you too.
- **Monsters:** the monsters placed in a place are run by one player's game; everyone's hits count,
  and a monster killed stays dead while someone is in that place.

## What stays yours

Your health, magic, rupees, ammunition and bottle contents, and where you are: joining does not move
you. Each player goes where they like.

## When a player leaves

The others play on. If the host leaves, the other players keep playing in their own Quest Logs.

## Limits

- Up to 4 players. The other players are shown about 60 ms behind, plus the network's delay.
- Friendly fire is on: a sword hit or an arrow on another player costs them half a heart (a heart for
  spin and jump attacks), but never their last quarter heart.
- Only tested with two copies on one Mac so far.
