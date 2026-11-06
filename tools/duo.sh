#!/bin/zsh
# SPDX-License-Identifier: MPL-2.0
# Online co-op on one Mac (after Galvakron/ZeldaWWHDRecompMP's tools/duo.sh, MPL-2.0): a host and a guest, both
# headless and muted, each with its own user, save, state and shader-cache folders, joined over 127.0.0.1.
#
#   tools/duo.sh NAME [SECONDS]      runs into $DUO_RUNS/NAME (default ../wt-online-runs), at most SECONDS (240)
#
# Each game boots into the save given by DUO_SAVE (a folder, copied; default the checkout's save/) with A presses, as
# the scripted tests do. HOST_ENV / GUEST_ENV: more variables for each (e.g. GUEST_ENV="WWHD_COOP_GAME=1234"). The
# guest starts GUEST_AFTER seconds (5) after the host. Each game's log (capped at 20 MB) and co-op trace
# (WWHD_MODS_TRACE) are host.log, host.trace, guest.log, guest.trace; WWHD_DUMP_FRAMES (DUO_FRAMES) writes pictures
# into host/ and guest/. A game over DUO_RSS_MB (6000) of memory is stopped. Only this script's own games are
# stopped, never another wwhd. DUO_SERVER=1: through a local wwhd-online-server (build/rel/server, WWHD_ONLINE=ON)
# on DUO_PORT, each game with its own identity; the host invites the guest by the friend code the guest writes, and the
# guest accepts (test aids); DUO_PARTY=0 leaves them strangers (presence only). DUO_KILL=host@S or guest@S stops that game S seconds after the guest started (a player
# leaving). DUO_UPLOAD: the server's upload limit per player (bytes/s); DUO_GAMES: the only games it takes.
set -u
root=${0:A:h:h}
name=$1 secs=${2:-240}
runs=${DUO_RUNS:-$root/../wt-online-runs}
out=$runs/$name
bin=${DUO_BIN:-$root/build/rel/wwhd}
save=${DUO_SAVE:-$root/save}
port=${DUO_PORT:-47777}
[ -x "$bin" ] || { echo "no game at $bin"; exit 1; }
[ -d "$save" ] || { echo "no save folder at $save"; exit 1; }
while [ "$(sysctl -n vm.loadavg | awk '{print int($2)}')" -gt 90 ]; do sleep 30; done
rm -rf "$out"; mkdir -p "$out/host" "$out/guest"
press=$(python3 -c "print(','.join('%d-%d:8000' % (f, f + 8) for f in range(1200, 3201, 60)))")
pids=()
run() {  # run ROLE COOP-VALUE ENV...
  local role=$1 coop=$2; shift 2
  cp -R "$save" "$out/$role/save"
  # (the shader prebuild, ~80 MB, is kept once per role for all runs, not in each run's user folder)
  mkdir -p "$out/$role/user" "$runs/duo-shader-prebuild-$role" && ln -s "$runs/duo-shader-prebuild-$role" "$out/$role/user/shader-prebuild"
  (cd "$out/$role" && exec env WWHD_HIDDEN_WINDOWS=1 WWHD_NO_HOST_INPUT=1 WWHD_NO_AUDIO=1 WWHD_NO_GAMEPAD=1 \
    WWHD_USER_DIR="$out/$role/user" WWHD_STATE_DIR="$out/$role/states" WWHD_SHADER_CACHE="$runs/duo-cache-$role.bin" \
    WWHD_VK_SHADER_CACHE="$runs/duo-vkshaders-$role.bin" WWHD_PRESS="$press" WWHD_COOP="$coop" WWHD_COOP_NAME=${(C)role} \
    WWHD_MODS_TRACE="$out/$role.trace" ${DUO_FRAMES:+WWHD_DUMP_FRAMES=$DUO_FRAMES} "$@" \
    "$bin" --game "$root/game" --save "$out/$role/save" 2>&1 | head -c 20000000 > "$out/$role.log") &
  pids+=($!)
}
trap 'for p in $pids; do pkill -9 -P $p 2>/dev/null; kill -9 $p 2>/dev/null; done' EXIT INT TERM HUP
if [ -n "${DUO_SERVER:-}" ]; then
  mkdir -p "$out/server"
  printf '[server]\nname = "duo"\n[network]\nbind = "127.0.0.1"\nport = %s\n[files]\nkey = "%s/server/server.key"\nadmin_socket = "%s/server/admin.sock"\nbans = "%s/server/bans.txt"\n[limits]\npow_bits = 8\nper_ip = 8\nbytes_per_s = %s\n' \
    "$port" "$out" "$out" "$out" "${DUO_UPLOAD:-8192}" > "$out/server/server.toml"
  # DUO_GAMES="0x..., 0x...": the server takes only these games (else any, each its own world)
  [ -n "${DUO_GAMES:-}" ] && printf '[network]\ngame_data = [%s]\n' "$DUO_GAMES" >> "$out/server/server.toml"
  (exec "$root/build/rel/server/wwhd-online-server" --config "$out/server/server.toml" 2>&1 | head -c 5000000 > "$out/server.log") &
  spid=$!
  sleep 1
  invite=; [ "${DUO_PARTY:-1}" = 1 ] && invite="WWHD_ONLINE_INVITE=@$out/guest.code"
  run host "" WWHD_ONLINE_SERVER=127.0.0.1:$port WWHD_ONLINE_IDENTITY=$out/host/id.key $invite ${=HOST_ENV:-}
  sleep ${GUEST_AFTER:-5}
  run guest "" WWHD_ONLINE_SERVER=127.0.0.1:$port WWHD_ONLINE_IDENTITY=$out/guest/id.key WWHD_ONLINE_CODE_FILE=$out/guest.code WWHD_ONLINE_ACCEPT=1 ${=GUEST_ENV:-}
else
  run host "host:$port" ${=HOST_ENV:-}
  sleep ${GUEST_AFTER:-5}
  run guest "join:127.0.0.1:$port" ${=GUEST_ENV:-}
fi
limit=${DUO_RSS_MB:-6000}
for i in $(seq 1 $secs); do
  alive=0
  for p in $pids; do
    for g in $(pgrep -P $p -x wwhd 2>/dev/null); do
      alive=1
      rss=$(ps -o rss= -p $g 2>/dev/null | tr -d ' ')
      [ -n "$rss" ] && [ $((rss / 1024)) -gt $limit ] && { echo "$name: game $g over ${limit} MB, stopped"; kill -9 $g; }
    done
  done
  if [ -n "${DUO_KILL:-}" ] && [ $i = ${DUO_KILL#*@} ]; then
    k=${DUO_KILL%@*}; [ $k = host ] && kp=${pids[1]} || kp=${pids[2]}
    for g in $(pgrep -P $kp -x wwhd 2>/dev/null); do echo "$name: $k stopped at ${i}s"; kill -9 $g; done
  fi
  [ $alive = 0 ] && [ $i -gt 10 ] && break
  sleep 1
done
for p in $pids; do for g in $(pgrep -P $p -x wwhd 2>/dev/null); do kill -9 $g; done; done
[ -n "${spid:-}" ] && { pkill -P $spid wwhd-online-server 2>/dev/null; kill $spid 2>/dev/null; }
wait 2>/dev/null
rm -rf "$out/host/save" "$out/guest/save"
echo "$name: host $(grep -c . "$out/host.trace" 2>/dev/null) trace lines, guest $(grep -c . "$out/guest.trace" 2>/dev/null)"
