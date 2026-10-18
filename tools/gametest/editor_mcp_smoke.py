#!/usr/bin/env python3
"""End-to-end check of the world editor's MCP interface (docs/mod-editor.md), as a tool client sees it.

Drives a running game that was started in the editor launch mode with the test-only switch
(WWHD_TEST_EDITOR_MCP=<port> and WWHD_TEST_EDITOR_MCP_TOKEN=<64 hex digits>; never set these outside
test runs). The token is read from the environment variable WWHD_TEST_EDITOR_MCP_TOKEN, never from the
command line. Standard library only.

    WWHD_TEST_EDITOR_MCP_TOKEN=... python3 tools/gametest/editor_mcp_smoke.py --port 47615 --out runs/mcp

Steps: initialize, tools/list (every expected tool), wait until Link is in the world and the editor view
is up, warp (to the open mod's first placed actor), add_actor at Link, move it, set its dialogue,
screenshot (saved as OUT/screenshot-tv.png), read_state, check_package, remove_actor without
"confirm" (must be refused, the actor stays) and command_log (the calls are marked MCP).
OUT/summary.json has every step's outcome; the exit status is 0 when all passed.
"""
import argparse
import base64
import http.client
import json
import os
import sys
import time

EXPECTED_TOOLS = ["warp", "warp_to", "list_locations", "add_actor", "move_actor", "set_dialogue", "remove_actor",
                  "read_state", "check_package", "command_log", "undo", "screenshot"]


class Client:
    def __init__(self, port, token):
        self.port, self.token, self.next_id = port, token, 1
        self.version = None

    def post(self, message, token=None, extra=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=180)
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
                   "Authorization": "Bearer " + (self.token if token is None else token)}
        if self.version:
            headers["MCP-Protocol-Version"] = self.version
        headers.update(extra or {})
        conn.request("POST", "/mcp", body=json.dumps(message), headers=headers)
        r = conn.getresponse()
        body = r.read()
        conn.close()
        return r.status, (json.loads(body) if body else None)

    def rpc(self, method, params=None):
        m = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        self.next_id += 1
        if params is not None:
            m["params"] = params
        status, body = self.post(m)
        if status != 200 or "error" in body:
            raise RuntimeError("%s: HTTP %d %s" % (method, status, body))
        return body["result"]

    def tool(self, name, args=None):
        r = self.rpc("tools/call", {"name": name, "arguments": args or {}})
        text = "".join(c.get("text", "") for c in r["content"] if c["type"] == "text")
        return r, text

    def command(self, name, args=None):
        """a command tool: (ok, parsed result or error text)"""
        r, text = self.tool(name, args)
        if r.get("isError"):
            return False, text
        try:
            return True, json.loads(text)
        except ValueError:
            return True, text


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=47615)
    ap.add_argument("--out", required=True)
    ap.add_argument("--wait", type=int, default=600, help="seconds to wait for the server and for Link in the world")
    ap.add_argument("--actor", default="mcp-sign", help="the id of the actor the run adds")
    args = ap.parse_args()
    token = os.environ.get("WWHD_TEST_EDITOR_MCP_TOKEN", "")
    if len(token) != 64:
        sys.exit("set WWHD_TEST_EDITOR_MCP_TOKEN (64 hex digits) as for the game")
    os.makedirs(args.out, exist_ok=True)
    c = Client(args.port, token)
    steps = []

    def step(name, ok, detail=""):
        steps.append({"step": name, "ok": bool(ok), "detail": detail})
        print("%-22s %s  %s" % (name, "ok  " if ok else "FAIL", str(detail)[:300]), flush=True)
        return ok

    # the server comes up with the editor (at start-up)
    deadline = time.time() + args.wait
    init = None
    while time.time() < deadline:
        try:
            init = c.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                        "clientInfo": {"name": "editor_mcp_smoke", "version": "1"}})
            break
        except (OSError, http.client.HTTPException):
            time.sleep(2)
    if not step("initialize", init and init.get("protocolVersion") == "2025-06-18", init and init.get("serverInfo")):
        return finish(args.out, steps)
    c.version = init["protocolVersion"]
    status, _ = c.post({"jsonrpc": "2.0", "method": "notifications/initialized"})
    step("initialized", status == 202, "HTTP %d" % status)
    status, _ = c.post({"jsonrpc": "2.0", "id": 99, "method": "tools/list"}, token="0" * 64)
    step("wrong token refused", status == 401, "HTTP %d" % status)
    status, _ = c.post({"jsonrpc": "2.0", "id": 99, "method": "tools/list"}, extra={"Origin": "https://evil.example"})
    step("foreign Origin refused", status == 403, "HTTP %d" % status)

    tools = c.rpc("tools/list")["tools"]
    names = {t["name"] for t in tools}
    missing = [t for t in EXPECTED_TOOLS if t not in names]
    step("tools/list", not missing, "%d tools%s" % (len(names), ", missing " + ", ".join(missing) if missing else ""))

    # Link in the world, the editor view up, nothing running
    state = None
    while time.time() < deadline:
        ok, state = c.command("read_state")
        if ok and state.get("link", {}).get("valid") and not state.get("busy") and state.get("fly", {}).get("on"):
            break
        time.sleep(3)
    if not step("in the world", state and state.get("link", {}).get("valid"), state and state.get("link")):
        return finish(args.out, steps)

    # warp: to the open mod's first placed actor (a Quest location), with the warp tool itself
    ok, locations = c.command("list_locations", {"group": "Quest"})
    target = next((l for l in (locations if ok else []) if l["id"].startswith("actor:")), None)
    if target:
        params = {"stage": target["stage"], "room": target["room"]}
        if "point" in target:
            params["point"] = target["point"]
        if "position" in target:
            params["position"] = target["position"]
            params["angle"] = target.get("angle", 0)
        ok, r = c.command("warp", params)
        step("warp", ok, "%s -> %s" % (target["id"], r))
        arrived = None
        t_end = time.time() + 180
        time.sleep(5)
        while time.time() < t_end:
            ok, s = c.command("read_state")
            link = s.get("link", {}) if ok else {}
            if ok and link.get("valid") and link.get("stage") == target["stage"] and not s.get("busy") and s.get("fly", {}).get("on"):
                arrived = link
                break
            time.sleep(3)
        step("arrived", arrived, arrived)
    else:
        step("warp", False, "no actor location in list_locations")

    ok, r = c.command("add_actor", {"type": "sign", "at": "link", "id": args.actor})
    step("add_actor", ok, r)
    ok, s = c.command("read_state")
    pos = s["link"]["position"]
    ok, r = c.command("move_actor", {"id": args.actor, "position": {"x": pos["x"] + 150, "y": pos["y"], "z": pos["z"]}})
    step("move_actor", ok, r)
    ok, r = c.command("place_actor", {"id": args.actor, "at": "ground"})
    step("place_actor (ground)", ok, r)
    ok, r = c.command("set_dialogue", {"id": args.actor, "lines": ["Placed over MCP.", "Hello from the smoke test!"]})
    step("set_dialogue", ok, r)
    ok, r = c.command("focus", {"id": args.actor})
    step("focus", ok, r)
    time.sleep(4)  # the sign appears and the camera turns

    r, text = c.tool("screenshot", {"screen": "tv"})
    image = next((x for x in r["content"] if x["type"] == "image"), None)
    if image and not r.get("isError"):
        png = base64.b64decode(image["data"])
        path = os.path.join(args.out, "screenshot-tv.png")
        with open(path, "wb") as f:
            f.write(png)
        width = int.from_bytes(png[16:20], "big")
        step("screenshot", png[:8] == b"\x89PNG\r\n\x1a\n" and 0 < width <= 1280, "%s (%d bytes, %d px wide)" % (path, len(png), width))
    else:
        step("screenshot", False, text)

    ok, s = c.command("read_state")
    step("read_state", ok and s.get("link", {}).get("valid"), {"link": s.get("link"), "progress": s.get("progress")} if ok else s)
    ok, r = c.command("check_package")
    step("check_package", ok, r)

    ok, r = c.command("remove_actor", {"id": args.actor})
    ok2, actors = c.command("list_actors")
    still = ok2 and any(a.get("id") == args.actor for a in actors)
    step("remove_actor refused", not ok and "confirm" in r and still, r)

    ok, log = c.command("command_log", {"count": 30})
    marked = [e["command"] for e in log if e.get("source") == "MCP"] if ok else []
    step("log marked MCP", "add_actor" in marked and "screenshot" in marked and "remove_actor" in marked, marked)
    return finish(args.out, steps)


def finish(out, steps):
    with open(os.path.join(out, "summary.json"), "w") as f:
        json.dump(steps, f, indent=2, default=str)
    failed = [s["step"] for s in steps if not s["ok"]]
    print("passed" if not failed else "FAILED: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
