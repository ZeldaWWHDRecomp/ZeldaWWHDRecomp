#!/bin/zsh
# SPDX-License-Identifier: MPL-2.0
# Online co-op soak test: two games through a local wwhd-online-server for a long session that mimics play
# (tools/duo.sh DUO_SERVER=1 with long scripted schedules), then a pass/fail summary.
#
#   tools/duo_soak.sh NAME [SECONDS]     runs into $DUO_RUNS/NAME (default ../wt-online-runs), SECONDS (3700) long
#
# What happens (both games hidden and muted, as duo.sh runs them; each in its own user/save/state folders):
#   - friendly fire on (the host's party switches 000D); the host faces the other player and cuts every 1.5 s, the
#     guest's hearts are set to half a heart every SOAK_LIFE_EVERY seconds (90): one death after another (a fairy,
#     then game over and Continue: A is pressed every 3 s);
#   - both walk about (stick segments from a fixed seed), the guest cuts too;
#   - stage and room changes: both warp through SOAK_STAGES every SOAK_WARP_EVERY seconds (240), each about the same
#     time; with DUO_STATES holding slot7.wwstate (a portable state on the boat) both load it every 4th place and sail
#     (WWHD_COOP_TEST_DRIFT);
#   - emotes every 50 s;
#   - disconnect/reconnect: the guest drops its connection every SOAK_GUEST_RECONNECT s (600), the host every
#     SOAK_HOST_RECONNECT s (1500), each back after 8 s ("Connect again");
#   - both games run hidden, so neither has the focus (as a game in the background).
# Pass: both games ran to the end (no crash, no exit), no kick, no protocol strike in the server log, no "slow down",
# "removed you" or crash report in the games' logs. The summary counts the events from the games' traces.
set -u
root=${0:A:h:h}
name=$1 secs=${2:-3700}
runs=${DUO_RUNS:-$root/../wt-online-runs}
out=$runs/$name
seed=${SOAK_SEED:-7}
stages=${SOAK_STAGES:-sea:44:0 M_NewD2:0:0 sea:11:0 Adanmae:0:0 sea:13:0 LinkRM:0:0}
warp_every=${SOAK_WARP_EVERY:-240}
gen() {  # gen ROLE: the schedule variables for one game
  python3 - "$1" "$secs" "$seed" "$warp_every" "${SOAK_LIFE_EVERY:-90}" "${DUO_STATES:+1}" ${=stages} <<'EOF'
import random, sys
role, secs, seed, warp_every, life_every, boat = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), sys.argv[5], sys.argv[6] == "1"
stages = sys.argv[7:]
rnd = random.Random(seed * 2 + (role == "guest"))
press, stick, warps, loads, emotes = [], [], [], [], []
t = 20.0
while t < secs:  # A every 3 s: dialogs, the game over menu's Continue
    press.append("%.2f-%.2f:8000" % (t, t + 0.15)); t += 3
cut = 1.5 if role == "host" else 7.0
t = 70.0
while t < secs:  # B: sword cuts (the host's at the guest: friendly fire)
    press.append("%.2f-%.2f:4000" % (t + 0.5, t + 0.65)); t += cut
import math
t = 70.0
while t < secs:  # walking: 1-2 s in a random direction and back again, then 3-6 s standing (the players stay close)
    d = rnd.uniform(1, 2); a = rnd.uniform(0, 6.283); x, y = math.cos(a) * 0.7, math.sin(a) * 0.7
    stick.append("%.2f-%.2f:%.2f:%.2f" % (t, t + d, x, y)); stick.append("%.2f-%.2f:%.2f:%.2f" % (t + d, t + 2 * d, -x, -y))
    t += 2 * d + rnd.uniform(3, 6)
k, t = 0, warp_every
while t < secs:  # places: TV frames (30 a second in a hidden game); the boat state every 4th place, when given
    if boat and k % 4 == 3: loads.append("%d:7" % int(t * 30))
    else: warps.append("%d:%s" % (int(t * 30), stages[k % len(stages)]))
    k += 1; t += warp_every
t, i = 45.0 + (role == "guest") * 20, 0
while t < secs:
    emotes.append("%.0f:%d" % (t, 1 + i % 5)); t += 50; i += 1
press.sort(key=lambda s: float(s.split("-")[0]))
print("WWHD_TEST_PRESS=" + ",".join(press))
print("WWHD_TEST_STICK=" + ",".join(stick))
print("WWHD_TEST_WARP_AT=" + ",".join(warps))
if loads: print("WWHD_PORTABLE_LOAD_AT=" + ",".join(loads))
print("WWHD_COOP_TEST_EMOTE=" + ",".join(emotes))
EOF
}
host_env=("${(@f)$(gen host)}") guest_env=("${(@f)$(gen guest)}")
common=(WWHD_COOP_NOTICE_OK=1 WWHD_COOP_TEST_DRIFT=20,10 WWHD_TEST_ORIGIN=1)  # (the timed input counts from TV frame 1)
export DUO_SERVER=1
export HOST_ENV="${common[*]} WWHD_ONLINE_PARTY_FLAGS=000D WWHD_COOP_TEST_PVP_AIM=60:$secs WWHD_COOP_TEST_RECONNECT=${SOAK_HOST_RECONNECT:-1500}:8 ${host_env[*]}"
export GUEST_ENV="${common[*]} WWHD_COOP_TUNIC=1 WWHD_COOP_TEST_LIFE=2@${SOAK_LIFE_EVERY:-90} WWHD_COOP_TEST_RECONNECT=${SOAK_GUEST_RECONNECT:-600}:8 ${guest_env[*]}"
start=$(date +%s)
"$root/tools/duo.sh" "$name" "$secs"
ran=$(( $(date +%s) - start ))
# the summary
fail=()
grep -q "^kick " "$out/server.log" && fail+=("kick")
grep -q "^strike " "$out/server.log" && fail+=("protocol strikes: $(grep -c '^strike ' "$out/server.log")")
for r in host guest; do
  grep -q "slow down\|removed you" "$out/$r.log" && fail+=("$r: told to slow down / removed")
  [ -n "$(find "$out/$r/user" -name 'crash-*' 2>/dev/null)" ] && fail+=("$r: crash report")
  last=$(tail -1 "$out/$r.trace" 2>/dev/null | cut -d' ' -f1)
  echo "$r: last logic step ${last:-?} ($(( ${last:-0} / 30 )) s of game time)"
  [ ${last:-0} -lt $(( (secs - 60) * 27 )) ] && fail+=("$r: stopped early (logic step ${last:-0})")
  grep -q "^CRASH\|Last reported runtime error" "$out/$r.log" && fail+=("$r: crash in the log")
  printf '%s: deaths %s, game overs/continues %s, scene changes %s, cuts that hit %s, emotes %s, drops %s, reconnects %s, sailing %s, poses not sent %s, stage codes cleaned %s\n' $r \
    "$(grep -c 'hearts [0-9]* -> 0 quarters' "$out/$r.trace")" "$(grep -c 'link proc [0-9]* -> 178' "$out/$r.trace")" \
    "$(grep -c 'scene change request' "$out/$r.trace")" "$(grep -c 'hits player' "$out/$r.trace")" \
    "$(grep -ci 'emote' "$out/$r.trace")" "$(grep -c 'coop: test: offline' "$out/$r.trace")" \
    "$(grep -c 'coop: test: online again' "$out/$r.trace")" "$(grep -c 'coop: sailing' "$out/$r.trace")" \
    "$(grep -c 'a pose not sent' "$out/$r.trace")" "$(grep -c 'kept with other bytes' "$out/$r.trace")"
done
[ $ran -lt $((secs - 30)) ] && fail+=("ended after ${ran}s of ${secs}s")
if [ ${#fail} = 0 ]; then echo "$name: PASS (${ran}s)"; else echo "$name: FAIL (${ran}s): ${(j:; :)fail}"; fi
