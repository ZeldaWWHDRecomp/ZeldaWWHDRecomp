#!/usr/bin/env python3
"""VirusTotal monitor for release artifacts (release.yml): uploads files, waits for the analyses and writes a
report. Monitor only: it never fails (exit status 0 on every error) and changes nothing in the release.

usage: virustotal.py --out vt-report.json [--summary FILE] [--timeout SECONDS] FILE|ZIP:MEMBER ...

FILE is uploaded as it is; ZIP:MEMBER uploads one member of a zip (the first whose path ends with MEMBER,
e.g. "dist/x-windows-x86_64.zip:Wind Waker HD.exe"). The API key comes from the environment (VT_API_KEY)
only; it is sent as the x-apikey header and never printed. Requests are spaced for the free tier (4 per
minute). Files over 32 MB go through /files/upload_url, as the API requires.
"""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request
import uuid
import zipfile

API = "https://www.virustotal.com/api/v3"
MIN_GAP = 16.0  # seconds between requests: the free tier allows 4 per minute
BIG = 32 * 1024 * 1024
_last = [0.0]


def notice(msg):
    print("::notice title=VirusTotal::" + msg.replace("\n", " "), flush=True)


def request(method, url, key, body=None, ctype=None, timeout=300):
    wait = _last[0] + MIN_GAP - time.time()
    if wait > 0:
        time.sleep(wait)
    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("x-apikey", key)
    req.add_header("accept", "application/json")
    if ctype:
        req.add_header("content-type", ctype)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode("utf-8"))
    finally:
        _last[0] = time.time()


def multipart(name, data):
    boundary = uuid.uuid4().hex
    head = ('--%s\r\nContent-Disposition: form-data; name="file"; filename="%s"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n" % (boundary, name.replace('"', "_"))).encode("utf-8")
    return head + data + ("\r\n--%s--\r\n" % boundary).encode(), "multipart/form-data; boundary=" + boundary


def load(spec):
    if os.path.isfile(spec):
        with open(spec, "rb") as f:
            return os.path.basename(spec), f.read()
    zpath, _, member = spec.rpartition(":")
    with zipfile.ZipFile(zpath) as z:
        for n in z.namelist():
            if n.endswith("/" + member) or n == member:
                return member, z.read(n)
    raise FileNotFoundError("%s not found in %s" % (member, zpath))


def upload(key, name, data):
    url = API + "/files"
    if len(data) > BIG:
        url = request("GET", API + "/files/upload_url", key)["data"]
    body, ctype = multipart(name, data)
    return request("POST", url, key, body, ctype)["data"]["id"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--summary")
    ap.add_argument("--timeout", type=float, default=600, help="seconds to wait for the analyses after the uploads")
    a = ap.parse_args()
    key = os.environ.get("VT_API_KEY", "").strip()
    report = {"files": []}
    if not key:
        notice("VT_API_KEY is not set (forks, pull requests): no scan")
        report["skipped"] = "no API key"
    else:
        items = []
        for spec in a.files:
            item = {"spec": spec}
            try:
                name, data = load(spec)
                item.update(name=name, size=len(data), sha256=hashlib.sha256(data).hexdigest())
                item["link"] = "https://www.virustotal.com/gui/file/" + item["sha256"]
                item["analysis"] = upload(key, name, data)
                print("uploaded %s (%d bytes, sha256 %s)" % (name, len(data), item["sha256"]), flush=True)
            except Exception as e:  # monitor only: report and go on
                item["error"] = "upload failed: %s" % e
                notice("%s: %s" % (spec, item["error"]))
            items.append(item)
        deadline = time.time() + a.timeout
        pending = [i for i in items if "analysis" in i]
        while pending and time.time() < deadline:
            for item in list(pending):
                try:
                    attrs = request("GET", API + "/analyses/" + item["analysis"], key)["data"]["attributes"]
                except Exception as e:
                    item["poll_error"] = str(e)
                    continue
                if attrs.get("status") == "completed":
                    item["stats"] = attrs.get("stats", {})
                    item["flagged"] = sorted(
                        "%s: %s (%s)" % (eng, r.get("result"), r.get("category"))
                        for eng, r in attrs.get("results", {}).items()
                        if r.get("category") in ("malicious", "suspicious"))
                    pending.remove(item)
                    print("%s: %d malicious, %d suspicious" % (item["name"], item["stats"].get("malicious", 0),
                                                               item["stats"].get("suspicious", 0)), flush=True)
        for item in pending:
            item["error"] = "analysis not completed within %d s" % a.timeout
            notice("%s: %s" % (item.get("name", item["spec"]), item["error"]))
        report["files"] = items
    with open(a.out, "w") as f:
        json.dump(report, f, indent=1)
    if a.summary:
        lines = ["## VirusTotal (monitor)", ""]
        if report.get("skipped"):
            lines.append("Skipped: " + report["skipped"])
        for i in report["files"]:
            lines.append("### %s" % i.get("name", i["spec"]))
            if "sha256" in i:
                lines.append("SHA-256 `%s`, %d bytes, [report](%s)" % (i["sha256"], i["size"], i["link"]))
            if "stats" in i:
                s = i["stats"]
                lines.append("")
                lines.append("**%d malicious, %d suspicious**, %d undetected, %d harmless, %d unsupported/failed" % (
                    s.get("malicious", 0), s.get("suspicious", 0), s.get("undetected", 0), s.get("harmless", 0),
                    s.get("type-unsupported", 0) + s.get("failure", 0) + s.get("timeout", 0)))
                lines += [""] + ["- " + f for f in i["flagged"]]
            if "error" in i:
                lines.append("")
                lines.append("Not available: " + i["error"])
            lines.append("")
        with open(a.summary, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    return 0


def render_notes(report_path):
    """the release-notes section for a finished report (publish follow-up job): links and counts per file"""
    rep = json.load(open(report_path))
    files = [i for i in rep.get("files", []) if "sha256" in i]
    if not files:
        return ""
    out = ["### VirusTotal", "",
           "Scan results of this release's files at publishing time (results can change as engines update):", ""]
    for i in files:
        s = i.get("stats")
        res = ("%d of %d engines flag it" % (s.get("malicious", 0) + s.get("suspicious", 0),
                                              sum(v for k, v in s.items() if k in ("malicious", "suspicious", "undetected", "harmless")))
               if s else "scan not finished")
        out.append("- `%s`: %s, [report](%s)" % (i["name"], res, i["link"]))
    flagged = sorted({f.split(":")[0] for i in files for f in i.get("flagged", [])})
    if flagged:
        out += ["", "Detections on the Windows files are generic heuristics on an unsigned program "
                "(%s), reported to the vendors as false positives; see issue #58." % ", ".join(flagged)]
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--render-notes":
        try:
            sys.stdout.write(render_notes(sys.argv[2]))
        except Exception as e:
            notice("render notes: %s" % e)
        sys.exit(0)
    try:
        sys.exit(main())
    except Exception as e:  # never fail the release
        notice("monitor error: %s" % e)
        sys.exit(0)
