# Online co-op (experimental)

Play *The Wind Waker HD* together with up to three friends. Each player runs their own copy of this
port with their own copy of the game. One player hosts, the others join.

The online co-op code comes from [Galvakron's ZeldaWWHDRecompMP](https://github.com/Galvakron/ZeldaWWHDRecompMP)
(MPL-2.0), co-op work by Galvakron and AsadSi.

## Before you start: privacy

Online co-op connects your game to the other players' games, directly or through a server. **Played
directly, they see your IP address, and you see theirs; through a server, its operator sees it.** An IP
address can reveal your approximate location. Only play with people you trust, on servers you trust.

**What a server keeps.** A server keeps your party's player keys (the keys behind your friend codes) and your
party's shared progress for up to 72 hours after your party last played, so your party survives a server restart.
The server's operator can set a different length. The server program does not write IP addresses to disk; if you
are banned, it may keep a scrambled form of yours. Whoever runs a server controls its data.

Nothing is sent over the network until you switch online co-op on and accept this notice. It stops
when you switch it off. When this notice changes, you are asked to accept it again.

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

## How to play

Open **Settings > Online** (Esc, or hold Select on a controller), or the **Online** tab of the settings page in the
pause menu.

1. Read the privacy notice and accept it.
2. Switch **Online co-op** on.
3. **Host on this computer**, or **Join a friend** and enter the host's address (an IP address or a name, e.g.
   a VPN name such as a Tailscale name).
4. Pick your name and tunic colour.

The host's UDP port (7777 unless you change it) must be reachable from the other players. On a VPN such as
Tailscale nothing else is needed. The status line shows who is connected; **Go to** takes you to a friend.

**Through a server** (builds made with `WWHD_ONLINE=ON`): choose **Through a server** and enter the address
of a `wwhd-online-server` you or a friend runs. Everyone on the server sees the others nearby; to play
together, form a party:

- **Your friend code** (e.g. `KQ7F-9MZ2-XD`) is shown on the Online page, with a **Copy** button. It belongs to
  this installation (a key file, `online-identity.key`, in your user folder) and stays the same on every server.
- Enter a friend's code under **Friend's code** and choose **Invite**. They see the invitation in the game and
  under **Invitations** on their Online page, and choose **Join** or **No** (in the pause menu: **Join an
  invitation** or **Decline the invitation**). A party has up to 4 players; **Leave the party** keeps you online.
- If the party's leader leaves, another member takes over.
- **Saved servers:** every server you connect to is saved (the 10 most recent), with its name. **Use** picks one
  again (in the pause menu, **Saved servers** steps through them); **Forget** removes it.
- The game remembers each server's key from your first visit. If a server at the same address later shows
  another key, the game does not connect and says so: it may be another server using that address. If the
  server's operator says the key changed, **Forget** the server and connect again.
- When the server refuses you or removes you, the status line and a message say why (another game version, a
  full server, invite-only, banned, removed by the operator, or sending too much) and what to do. The game
  does not try again by itself after such a stop; choose **Connect again** (or switch online off and on).

**Stay together** (off by default) plays the fork's way: you play in the host's world, your own saves are off,
going somewhere the host isn't asks the host first, and you go back to the title screen when the host leaves.

For scripted tests the environment variables `WWHD_COOP=host[:port]` or `WWHD_COOP=join:<address>[:port]`,
`WWHD_COOP_NOTICE_OK=1`, `WWHD_COOP_NAME` and `WWHD_COOP_TUNIC=0..3` do the same.

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

**What stays your own:** your hearts and magic, your rupees (unless the party shares gains), arrows, bombs,
bottle contents, bags, item buttons, the time of day, where you start after loading, your name and options,
side-quest counters and minigame records, a dungeon's or room's temporary state (moving platforms, water,
blocks you pushed), and things the game resets on its own (what happened today or this week, one visit to a
place). Story changes another player makes while you are in a cutscene or talking to someone reach you when it
ends.

## Boats

Everyone sails their own boat, and the others see it with you in it. A friend in your party can ride along
on your boat: when you sail and they are close by (swimming or standing, not in their own boat), they choose
**Go to** you and land on the back of your boat. They stay on it while you sail and turn, and jump off to
leave. If you stop sailing, go out of sight or leave the party, the boat is gone under their feet and they
drop into the sea. Riding needs the rider's own boat somewhere in the sea (the game uses it to hold the
deck), so it works once you have the King of Red Lions.

## Players further away

Through a server, players close to you are drawn in full detail. Players further off in the same area are drawn as a
simpler Link, standing, walking or running. At sea, players too far away to draw show as dots on a small radar in
the lower left corner, with party members in green. You can turn the radar off in Settings > Online.

## Blocking and muting

Through a server you can block or mute players outside your party. Use the list of nearby players in
Settings > Online, or **Block the nearest player** / **Mute the nearest player** on the Online page of the pause
menu.

- **Blocked:** you and they don't see each other any more. No Link, sounds, effects, emotes or radar dot, and
  the server stops passing their messages to you.
- **Muted:** you still see them, but hear no sounds and see no emotes from them.

The list is kept between sessions and on every server. Undo an entry in Settings > Online. Party members can't be
blocked; leave the party instead.

## Emotes

While you're online, press **Alt+1** to **Alt+5** on the keyboard, or pick an emote under **Emotes** in
Settings > Online or on the Online page of the pause menu: Wave, Cheer, Yes, Surprised or Look around. The other players see your Link do it. Players near you who
aren't in your party see it too. Your own Link doesn't move. A small bubble over his head shows the emote while
the others see it. You can change the keys in Settings > Controls, under Online. You can play one emote a second. If you walk away, the emote stops. The animations come from
your own game files.

## Mods

Only mods that change how the game plays count online. Those are built-in mods like Jump anywhere, Climb any wall
or the traversal speeds; packages whose author marked them as gameplay mods; and a randomized game.
Graphics packs, the camera mods, texts and other mods that only change what you see are never sent or checked.

- **In a party** everyone plays with the leader's gameplay mods: the same mods, in the same version, with the same
  settings. If yours differ, you can't join, and Settings > Online lists each mod that differs. If you have every
  mod the party uses installed, **Use the party's mods** turns them on and your other gameplay mods off. Some mods
  need a restart for that; the page says so. Then connect again.
- **Players outside your party** are never checked.
- **A server** can also set rules: no gameplay mods, only some, some required or some banned. **Mod rules** next to
  a server in Settings > Online shows its rules before you join. If it turns you away, the Online page names each
  mod it doesn't accept. Nothing is ever downloaded from a server.

## What stays yours

Your health, magic, rupees, ammunition and bottle contents, and where you are: joining does not move
you. Each player goes where they like.

## When a player leaves

The others play on. If the host leaves, one of the other players becomes the host and the rest join them;
with only two players, the one left keeps playing in their own Quest Log.

Through a server you can join and leave a party as often as you like while you play, with no loading and no
trip to the title screen. Whoever leaves keeps playing where they are, in their own Quest Log, and can save.
Through a server, the party's shared progress is also kept by the server (for as long as the privacy notice
says), so a friend who comes back while nobody else is online still gets what the others did. When you join
(again), what you did while apart is merged both ways: the story, chests, items and maps that
either of you got are then in both games. Small keys follow the party's leader. With **Stay together** on,
leaving the party takes you back to the title screen, because you were playing in the host's world.

## Limits

- Up to 4 players in a party. Through a server, up to 31 other players nearby are shown on a computer (fewer on
  a phone). The other players are shown about 60 ms behind, plus the network's delay.
- **Players outside your party** (Settings > Online): **Like any Link**, or **See-through**: drawn half
  transparent, so you tell them from your party, who always look solid.
- Each other player shown takes about 0.5 MB of the port's own memory the first time; the game's memory is not
  used for them.
- Friendly fire: a sword hit or an arrow on another player costs them half a heart (a heart for spin and
  jump attacks), but never their last quarter heart. Played directly it is on; through a server it is a party
  switch, off unless the leader turns it on.
- **Party switches** (through a server; the party's leader sets them on the Online page, every member sees
  them): shared progress (on), shared rupees (off), friendly fire (off), stay together (off).
- **Networks that block UDP:** if the server doesn't answer over UDP within about 3 seconds, the game connects to
  the same address over TCP instead. The status line then says "(TCP)". It works the same, but others may move
  less smoothly on a slow connection. Playing directly (without a server) needs UDP.
- Only tested with two copies on one Mac so far.
