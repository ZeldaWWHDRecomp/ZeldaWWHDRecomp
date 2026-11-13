#!/usr/bin/env python3
"""The game for an Apple TV, iPhone or iPad, built on this Mac (setup.py --apple-device tv|iphone).

These devices run only code signed into an app and have no compiler, so the Mac builds the app from your own
game, as setup builds the Mac's game:

  1. the game code setup translated for this Mac (from its translation cache), compiled for the device
     (Xcode's tvOS / iOS SDK) and linked with the release's runtime for it (sdk/apple/<tvos|ios>);
  2. the app: your game files (copies that take no space on APFS), the shader library compiled for the
     device from your game's shaders (a Metal warm of this Mac's game writes them) and this Mac's shader
     cache, the icon from the game's boot image, and your switched-on code mods (bundle_mods.py);
  3. signed with your developer team's profile for the device (Xcode, signed in to a paid Apple Developer
     account: a team provisioning profile), and installed on a device chosen from the paired ones
     (xcrun devicectl).

The app contains code and files from your game: it is for your own devices and must not be distributed.

Needs Xcode (with the tvOS or iOS platform: Xcode > Settings > Components, or
`xcodebuild -downloadPlatform tvOS`), and Xcode 26 or newer for the shader library.
"""
import glob
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
KINDS = {  # --apple-device name: (sdk/apple folder, SDK, devicectl platform, bundle id suffix)
    "tv": ("tvos", "appletvos", "tvOS", "tv"),
    "iphone": ("ios", "iphoneos", "iOS", "ipad"),
}
LABELS = {"tv": "Apple TV", "iphone": "iPhone or iPad"}
PROFILE_DIRS = ["~/Library/Developer/Xcode/UserData/Provisioning Profiles", "~/Library/MobileDevice/Provisioning Profiles"]


class Error(RuntimeError):
    pass


# ---------------------------------------------------------------------------------------------------------- Xcode

def developer_dir():
    """Xcode's developer folder (the active one can be the Command Line Tools, which have no device SDKs)."""
    if os.environ.get("DEVELOPER_DIR"):
        return os.environ["DEVELOPER_DIR"]
    try:
        active = subprocess.run(["xcode-select", "-p"], capture_output=True, text=True).stdout.strip()
    except OSError:
        active = ""
    if active.endswith(".app/Contents/Developer"):
        return active
    apps = sorted(glob.glob("/Applications/Xcode*.app/Contents/Developer"))
    if "/Applications/Xcode.app/Contents/Developer" in apps:
        return "/Applications/Xcode.app/Contents/Developer"
    if apps:
        return apps[-1]
    raise Error("Building for an Apple TV, iPhone or iPad needs Xcode (from the App Store); the Command Line "
                "Tools alone have no tvOS or iOS SDK")


def xcode_env(dev):
    return dict(os.environ, DEVELOPER_DIR=dev)


def sdk_path(env, sdk, platform):
    p = subprocess.run(["xcrun", "--sdk", sdk, "--show-sdk-path"], env=env, capture_output=True, text=True)
    if p.returncode != 0 or not p.stdout.strip():
        raise Error("Xcode has no %s SDK yet: add it in Xcode > Settings > Components, or run "
                    "xcodebuild -downloadPlatform %s" % (platform, platform))
    return p.stdout.strip()


# ------------------------------------------------------------------------------------------------------- signing

def _plist_of_profile(path):
    p = subprocess.run(["security", "cms", "-D", "-i", path], capture_output=True)
    if p.returncode != 0:
        return None
    try:
        return plistlib.loads(p.stdout)
    except Exception:
        return None


def signing_identities():
    """{SHA-1 of the certificate: name} of the code-signing identities in the keychain"""
    out = subprocess.run(["security", "find-identity", "-v", "-p", "codesigning"], capture_output=True,
                         text=True).stdout
    ids = {}
    for line in out.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3 and len(parts[1]) == 40:
            ids[parts[1].upper()] = parts[2].strip('"')
    return ids


def find_profile(platform, bundle_id, team=None, now=None):
    """The best development profile for the app on PLATFORM ("tvOS" / "iOS"): one of TEAM (any team when None)
    whose App ID covers BUNDLE_ID, not expired, with a certificate whose identity is in the keychain.
    Returns (profile path, its plist, signing identity SHA-1) or raises."""
    import datetime
    now = now or datetime.datetime.now(datetime.timezone.utc)
    identities = signing_identities()
    seen, best = [], None
    for d in PROFILE_DIRS:
        for path in glob.glob(os.path.join(os.path.expanduser(d), "*.mobileprovision")):
            prof = _plist_of_profile(path)
            if not prof:
                continue
            ents = prof.get("Entitlements", {})
            prof_team = (prof.get("TeamIdentifier") or [""])[0]
            app_id = ents.get("application-identifier", "")
            seen.append("%s (%s, %s)" % (prof.get("Name", "?"), prof_team, ", ".join(prof.get("Platform", []))))
            if platform not in prof.get("Platform", []) or (team and prof_team != team):
                continue
            expires = prof.get("ExpirationDate")
            if expires and expires.replace(tzinfo=expires.tzinfo or datetime.timezone.utc) <= now:
                continue
            pattern = app_id[len(prof_team) + 1:] if app_id.startswith(prof_team + ".") else ""
            if not (pattern == "*" or pattern == bundle_id or (pattern.endswith(".*") and bundle_id.startswith(pattern[:-1]))):
                continue
            if not ents.get("get-task-allow"):
                continue  # a distribution profile: devices need a development one
            import hashlib
            certs = [hashlib.sha1(c).hexdigest().upper() for c in prof.get("DeveloperCertificates", [])]
            identity = next((c for c in certs if c in identities), None)
            if not identity:
                continue
            exact = pattern == bundle_id
            rank = (exact, expires or now)
            if not best or rank > best[0]:
                best = (rank, path, prof, identity)
    if not best:
        raise Error("No development profile for %s apps%s with its certificate in this Mac's keychain. Open Xcode, "
                    "sign in to your Apple Developer account (Settings > Accounts) and let it make the team's "
                    "profile: e.g. build any %s app for your device once. Profiles found: %s"
                    % (platform, " of team " + team if team else "", platform, "; ".join(seen) or "none"))
    return best[1], best[2], best[3]


def entitlements(prof, bundle_id):
    team = prof["TeamIdentifier"][0]
    return {"application-identifier": "%s.%s" % (team, bundle_id), "com.apple.developer.team-identifier": team,
            "get-task-allow": True}


def sign(app, prof_path, prof, identity, bundle_id, env, log):
    shutil.copyfile(prof_path, os.path.join(app, "embedded.mobileprovision"))
    ent = os.path.join(os.path.dirname(app), "entitlements.plist")
    with open(ent, "wb") as f:
        plistlib.dump(entitlements(prof, bundle_id), f)
    for lib in sorted(glob.glob(os.path.join(app, "GuestMods", "*.dylib"))):
        run(["codesign", "--force", "--sign", identity, "--timestamp=none", lib], env, log, "signing " + os.path.basename(lib))
    run(["codesign", "--force", "--sign", identity, "--entitlements", ent, "--timestamp=none",
         "--generate-entitlement-der", app], env, log, "signing the app")
    run(["codesign", "--verify", "--deep", "--strict", app], env, log, "checking the signature")


# ------------------------------------------------------------------------------------------------------- devices

def devices(platform, env):
    """this Mac's paired devices of PLATFORM ("tvOS" / "iOS"; None: both): [{id, name, model, platform, os,
    reachable}]. Simulators are left out; a device is reachable when devicectl has a way to it (the local
    network or a cable), whatever its tunnel state (it connects on demand)."""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "devices.json")
        p = subprocess.run(["xcrun", "devicectl", "list", "devices", "--json-output", out], env=env, capture_output=True, text=True)
        if p.returncode != 0 or not os.path.isfile(out):
            raise Error("Listing the paired devices failed (xcrun devicectl list devices): %s" % (p.stderr or p.stdout).strip()[-400:])
        with open(out) as f:
            data = json.load(f)
    found = []
    for d in data.get("result", {}).get("devices", []):
        hw, props, conn = d.get("hardwareProperties", {}), d.get("deviceProperties", {}), d.get("connectionProperties", {})
        wanted = (platform,) if platform else ("tvOS", "iOS")
        if hw.get("reality") != "physical" or hw.get("platform") not in wanted:
            continue
        found.append({"id": hw.get("udid") or d.get("identifier"), "name": props.get("name", "?"),
                      "model": hw.get("marketingName") or hw.get("productType", ""), "platform": hw.get("platform"),
                      "os": props.get("osVersionNumber", ""),
                      "reachable": bool(conn.get("transportType")) and conn.get("tunnelState") != "unavailable"})
    return found


def running(device, executable, env):
    """whether the app's EXECUTABLE runs on DEVICE; raises when the device can't say (never guesses "no")"""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "processes.json")
        p = subprocess.run(["xcrun", "devicectl", "device", "info", "processes", "--device", device, "--json-output", out],
                           env=env, capture_output=True, text=True)
        if p.returncode != 0 or not os.path.isfile(out):
            raise Error("Couldn't check whether the game is running on the device: %s" % (p.stderr or p.stdout).strip()[-400:])
        with open(out) as f:
            procs = json.load(f).get("result", {}).get("runningProcesses", [])
    if not procs:
        raise Error("Couldn't check whether the game is running on the device (no processes listed)")
    return any(str(x.get("executable", "")).rstrip("/").endswith("/" + executable) for x in procs)


# --------------------------------------------------------------------------------------------------------- build

def run(cmd, env, log, what):
    log("$ " + " ".join(cmd))
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", "replace")
    if p.returncode != 0:
        log(out)
        raise Error("%s failed:\n%s" % (what, "\n".join(out.strip().splitlines()[-15:])))
    return out


def code_mods_hooks(data_dir):
    """the Mac's code-mod choice: game code with mod hooks when its current build has them"""
    try:
        with open(os.path.join(data_dir, "code-mods-active.json")) as f:
            active = json.load(f)
        return bool(active.get("hooks")) and active.get("state") == "ready"
    except (OSError, ValueError):
        return False


def user_dir(setup, ctx):
    """the game's user folder on this Mac (settings, mod manager, shader preparation)"""
    import shader_prebuild
    return str(shader_prebuild.default_cache_root(ctx.data_dir, setup.PORTABLE).parent)


def shader_generation(data_dir, user_dir):
    """this Mac's active shader generation folder (None when setup had no shader template to prepare one: then
    only the shaders the Mac's game has met go in) and its live cache folder (shader_prebuild.py)"""
    root = Path(user_dir) / "shader-prebuild"
    live = root / "live"
    try:
        active = json.loads((root / "active.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        active = {}
    gen = root / active.get("generation", "")
    if active.get("generation") and (gen / "headstart.bin").is_file():
        return gen, live
    if (live / "shaders.bin").is_file() and (live / "shaders.bin").stat().st_size > 0:
        return None, live
    raise Error("This Mac's game has no shaders yet: start the game and play for a while first")


def build_shader_library(setup, ctx, sdk, out, env, log):
    """the device's shaders.metallib: this Mac's game writes every shader's source (a Metal warm, as shader
    preparation does for the Mac's own library), Xcode's compiler builds them for SDK"""
    import shader_prebuild
    import shader_library
    if shader_library.xcode_major() < 26:
        raise Error("The shader library needs Xcode 26 or newer")
    if not shader_library.toolchain_available(sdk):
        shader_library.ensure_toolchain(log=log, sdk=sdk)
    gen, live = shader_generation(ctx.data_dir, user_dir(setup, ctx))
    exe = setup_game_exe(setup, ctx)
    sources = Path(out).parent / "shader-sources"
    shutil.rmtree(sources, ignore_errors=True)
    sources.mkdir(parents=True)
    try:
        if gen:
            wenv = dict(os.environ, **shader_prebuild.cache_environment(gen, ctx.game_dir, "metal"))
        else:
            # no head start: the warm writes the live cache's shaders only
            wenv = dict(os.environ, **shader_prebuild.cache_environment(live.parent / "no-generation", ctx.game_dir, "metal"))
            wenv["WWHD_HEADSTART"] = "0"
            log("no prepared shader generation: the library holds only the shaders this Mac's game has met")
        wenv.update(WWHD_SHADERLIB_SOURCES=str(sources), WWHD_SHADERLIB="0", WWHD_RENDERER="metal")
        log("writing the game's shaders (Metal warm of %s)" % exe)
        shader_prebuild.run_process([exe, "--game", ctx.game_dir, "--warm-shaders"], wenv, lambda: False, lambda line: None)
        built, failed = shader_library.build(str(sources), str(out), sdk=sdk)
        log("shader library for %s: %d shaders, %d failed" % (sdk, built, failed))
    finally:
        shutil.rmtree(sources, ignore_errors=True)


def setup_game_exe(setup, ctx):
    """this Mac's installed game"""
    try:
        with open(os.path.join(ctx.data_dir, "code-mods-active.json")) as f:
            exe = json.load(f).get("exe", "")
        if exe and os.path.isfile(exe):
            return exe
    except (OSError, ValueError):
        pass
    exe = os.path.join(ctx.data_dir, "bin", ctx.manifest["exe"])
    if not os.path.isfile(exe):
        raise Error("The game isn't installed on this Mac yet: run setup first")
    return exe


# ------------------------------------------------------------------------------------- what the setup app shows
# report() and device_state() answer everything the setup app (and --apple-status) shows before a build: each
# check is {ok, text, fix}; nothing in them changes anything. build_app() and install_app() do the work.

def check(ok, good, bad=None, fix=""):
    """{ok, text (GOOD, or BAD when not ok), fix (what to do, only when not ok)}"""
    return {"ok": bool(ok), "text": good if ok else (bad or good), "fix": "" if ok else fix}


def xcode_info():
    """the Xcode setup builds with: {path, developer_dir, version}, or None"""
    try:
        dev = developer_dir()
    except Error:
        return None
    version = ""
    try:
        with open(os.path.join(os.path.dirname(dev), "version.plist"), "rb") as f:
            version = str(plistlib.load(f).get("CFBundleShortVersionString", ""))
    except (OSError, ValueError, plistlib.InvalidFileException):
        pass
    return {"path": os.path.dirname(os.path.dirname(dev)), "developer_dir": dev, "version": version}


def major(version):
    try:
        return int(str(version).split(".")[0])
    except ValueError:
        return 0


def sdk_version(env, sdk):
    p = subprocess.run(["xcrun", "--sdk", sdk, "--show-sdk-version"], env=env, capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else ""


def simulator_runtime(env, platform, version):
    """a simulator runtime of PLATFORM for the SDK VERSION is installed: Xcode's asset compiler (the icon) needs one"""
    p = subprocess.run(["xcrun", "simctl", "list", "runtimes", "--json"], env=env, capture_output=True, text=True)
    try:
        runtimes = json.loads(p.stdout).get("runtimes", [])
    except ValueError:
        return False
    want = ".".join(version.split(".")[:2])
    return any(r.get("platform") == platform and r.get("isAvailable") and
               ".".join(str(r.get("version", "")).split(".")[:2]) == want for r in runtimes)


def apple_tool(setup, name):
    """a tool of the release's tools/apple (tvos/ in a source checkout)"""
    for d in (os.path.join(setup.PKG, "tools", "apple"), os.path.join(HERE, "..", "..", "tvos")):
        if os.path.isfile(os.path.join(d, name)):
            return os.path.normpath(os.path.join(d, name))
    raise Error("%s is missing from this release" % name)


def mod_plan(setup, ctx):
    """the code mods the app would get: {on, included: [{id, name}], left_out: [{id, name, reason}], profile}
    (profile: id -> on, as the app's mod manager starts with it)"""
    if not code_mods_hooks(ctx.data_dir):
        return {"on": False, "included": [], "left_out": [], "profile": {}}
    manager = Path(user_dir(setup, ctx)) / "ModManager"
    if not (manager / "profiles.json").is_file():
        return {"on": True, "included": [], "left_out": [], "profile": {}}
    import importlib.util
    spec = importlib.util.spec_from_file_location("bundle_mods", apple_tool(setup, "bundle_mods.py"))
    bundle_mods = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle_mods)
    profiles, mods, left_out = bundle_mods.plan(manager)
    included = []
    for mod_id, package, _ in mods:
        try:
            name = json.loads((package / "manifest.json").read_text(encoding="utf-8")).get("name") or mod_id
        except (OSError, ValueError):
            name = mod_id
        included.append({"id": mod_id, "name": name})
    profile = dict(profiles["profiles"][profiles["active"]].get("enabled", {}))
    return {"on": True, "included": included, "left_out": left_out, "profile": profile}


def prepare_mod_addresses(setup, ctx, log=print, wait=180):
    """Starts this Mac's game until its mod manager has given each switched-on guest mod its address (the
    app's modules are built for those addresses; the size includes the mod's heap, which only the game
    knows), then closes it. A mod still without one is left out of the app, and the plan says why."""
    missing = [m for m in mod_plan(setup, ctx)["left_out"] if m.get("needs_address")]
    if not missing:
        return
    # the installed game (bin), which starts the code-mods build with code mods on
    exe = os.path.join(ctx.data_dir, "bin", ctx.manifest["exe"])
    if any(subprocess.run(["pgrep", "-f", e], stdout=subprocess.DEVNULL).returncode == 0
           for e in {exe, setup_game_exe(setup, ctx)}):
        log("The game is open on this Mac: quit it and build again to include %s" % ", ".join(m["name"] for m in missing))
        return
    profiles = Path(user_dir(setup, ctx)) / "ModManager" / "profiles.json"
    want = {m["id"] for m in missing}

    def have():
        try:
            return want <= set(json.loads(profiles.read_text(encoding="utf-8")).get("guest_regions", {}))
        except (OSError, ValueError):
            return False
    log("Starting the game on this Mac to set up %s for the app (it closes again by itself)"
        % ", ".join(m["name"] for m in missing))
    game = subprocess.Popen([exe, "--game", ctx.game_dir, "--save", os.path.join(ctx.data_dir, "save")], cwd=ctx.data_dir,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        end = time.time() + wait
        while not have() and game.poll() is None and time.time() < end:
            time.sleep(1)
        closed = game.poll() is not None
    finally:
        if game.poll() is None:
            game.terminate()
            try:
                game.wait(timeout=10)
            except subprocess.TimeoutExpired:
                game.kill()
                game.wait()
    if not have():
        log("The game on this Mac didn't set them up (it %s)" % ("closed early" if closed else "took too long"))


def built_app(ctx, which):
    """the app built here last: {path, version, build, mods, signed}, or None"""
    app = Path(ctx.data_dir) / "apple" / KINDS[which][0] / "WindWakerHD.app"
    try:
        with open(app / "Info.plist", "rb") as f:
            info = plistlib.load(f)
    except (OSError, ValueError, plistlib.InvalidFileException):
        return None
    return {"path": str(app), "version": info.get("CFBundleShortVersionString", ""), "build": info.get("CFBundleVersion", ""),
            "mods": info.get("WWHDMods", []), "signed": (app / "embedded.mobileprovision").is_file()}


def report(setup, ctx, team=None, bundle_prefix="org.wwhdrecomp"):
    """Everything that decides whether the apps can be built and installed here, and what they would hold:
    {xcode: {check, version, path}, mac: {game, shaders}, mods: mod_plan(), devices_error,
     apps: {tv|iphone: {label, platform, bundle_id, checks: {release, sdk, shader_compiler, icon, signing},
                        signing: {...}, ready, problems, app: built_app(), devices: [...]}}}"""
    out = {"apps": {}}
    xc = xcode_info()
    env = xcode_env(xc["developer_dir"]) if xc else None
    if not xc:
        out["xcode"] = {"check": check(False, "", "Xcode isn't installed (the Command Line Tools have no tvOS or iOS SDK)",
                                       "Install Xcode from the App Store, open it once, then check again")}
    else:
        new_enough = major(xc["version"]) >= 26
        out["xcode"] = {"version": xc["version"], "path": xc["path"],
                        "check": check(new_enough, "Xcode %s" % xc["version"],
                                       "Xcode %s is too old: the shader library needs Xcode 26 or newer" % xc["version"],
                                       "Update Xcode from the App Store")}
    # this Mac's game: the app is built from it
    try:
        exe = setup_game_exe(setup, ctx)
        game = check(setup.valid_game_folder(ctx.game_dir), "The game is installed on this Mac",
                     "The game files are missing on this Mac", "Run setup with your game first")
    except Error as e:
        game = check(False, "", str(e), "Run setup with your game first")
    try:
        shader_generation(ctx.data_dir, user_dir(setup, ctx))
        gen, _ = shader_generation(ctx.data_dir, user_dir(setup, ctx))
        shaders = check(True, "This Mac's shaders are prepared" if gen else
                        "This Mac's game has met some shaders (no prepared shaders: the rest compile on the device)")
    except Error as e:
        shaders = check(False, "", str(e), "Start the game on this Mac and play for a while")
    out["mac"] = {"game": game, "shaders": shaders}
    try:
        out["mods"] = mod_plan(setup, ctx)
    except (Error, OSError, ValueError, KeyError) as e:
        out["mods"] = {"on": True, "included": [], "left_out": [], "profile": {}, "error": str(e)}
    found = []
    if env:
        try:
            found = devices(None, env)
        except Error as e:
            out["devices_error"] = str(e)
    for which, (kind, sdk, platform, suffix) in KINDS.items():
        bundle_id = "%s.%s" % (bundle_prefix, suffix)
        a = {"label": LABELS[which], "platform": platform, "bundle_id": bundle_id, "checks": {}, "signing": None,
             "app": built_app(ctx, which), "devices": [d for d in found if d["platform"] == platform]}
        c = a["checks"]
        c["release"] = check(ctx.manifest.get("apple", {}).get(kind), "This release has the %s app" % platform,
                             "This release has no %s app" % platform, "Use a macOS release that includes it")
        if env:
            version = sdk_version(env, sdk)
            c["sdk"] = check(version, "%s SDK %s" % (platform, version), "Xcode has no %s SDK" % platform,
                             "Xcode > Settings > Components: add %s, or run: xcodebuild -downloadPlatform %s" % (platform, platform))
            import shader_library
            have = shader_library.toolchain_available(sdk)
            c["shader_compiler"] = check(True, "The Metal compiler is installed" if have else
                                         "The Metal compiler is downloaded through Xcode during the build")
            c["icon"] = check(version and simulator_runtime(env, platform, version),
                              "The %s %s Simulator is installed (for the app's icon)" % (platform, version),
                              "The %s %s Simulator isn't installed: Xcode needs it to make the app's icon" % (platform, version),
                              "Xcode > Settings > Components: add the %s %s Simulator" % (platform, version))
        try:
            prof_path, prof, identity = find_profile(platform, bundle_id, team)
            names = signing_identities()
            a["signing"] = {"profile": prof.get("Name"), "team": prof["TeamIdentifier"][0],
                            "team_name": prof.get("TeamName", ""), "identity": names.get(identity, identity),
                            "expires": str(prof.get("ExpirationDate", ""))}
            c["signing"] = check(True, "Signing as %s (%s), profile %s until %s" % (
                a["signing"]["identity"], a["signing"]["team_name"] or a["signing"]["team"], a["signing"]["profile"],
                str(prof.get("ExpirationDate", ""))[:10]))
        except Error as e:
            c["signing"] = check(False, "", "No %s development profile with its certificate in this Mac's keychain" % platform,
                                 "Open Xcode, sign in to your Apple Developer account (Settings > Accounts), and build "
                                 "any %s app for your device once so Xcode makes the team's profile" % platform)
        needed = [out["xcode"]["check"], game, shaders] + list(c.values())
        a["problems"] = [x["text"] for x in needed if not x["ok"]]
        a["ready"] = not a["problems"]
        out["apps"][which] = a
    return out


def device_state(setup, ctx, which, device, bundle_prefix="org.wwhdrecomp"):
    """One device, before installing on it: {device, running: bool|None, running_error, installed: {version,
    build}|None, mods_on_device: [ids]|None, switched_off: [names], switched_on: [names]} (the mods an install
    would switch off or on there: the app's profile replaces the device's when the app's mods change)."""
    kind, sdk, platform, suffix = KINDS[which]
    xc = xcode_info()
    if not xc:
        raise Error("Xcode isn't installed")
    env = xcode_env(xc["developer_dir"])
    target = pick_device(platform, device, env)
    bundle_id = "%s.%s" % (bundle_prefix, suffix)
    exe = ctx.manifest.get("apple", {}).get(kind, {}).get("exe", "WindWakerHD")
    out = {"device": target, "running": None, "installed": None, "mods_on_device": None, "switched_off": [], "switched_on": []}
    try:
        out["running"] = running(target["id"], exe, env)
    except Error as e:
        out["running_error"] = str(e)
    with tempfile.TemporaryDirectory() as tmp:
        apps = os.path.join(tmp, "apps.json")
        p = subprocess.run(["xcrun", "devicectl", "device", "info", "apps", "--device", target["id"], "--bundle-id", bundle_id,
                            "--json-output", apps], env=env, capture_output=True, text=True)
        if p.returncode == 0 and os.path.isfile(apps):
            with open(apps) as f:
                listed = json.load(f).get("result", {}).get("apps", [])
            if listed:
                out["installed"] = {"version": listed[0].get("version", ""), "build": listed[0].get("bundleVersion", "")}
        if out["installed"]:
            prof = os.path.join(tmp, "profiles.json")
            p = subprocess.run(["xcrun", "devicectl", "device", "copy", "from", "--device", target["id"],
                                "--domain-type", "appDataContainer", "--domain-identifier", bundle_id,
                                "--source", "Library/Caches/user/ModManager/profiles.json", "--destination", prof],
                               env=env, capture_output=True, text=True)
            try:
                with open(prof, encoding="utf-8") as f:
                    profiles = json.load(f)
                on = profiles["profiles"][profiles["active"]].get("enabled", {})
                out["mods_on_device"] = sorted(k for k, v in on.items() if v)
            except (OSError, ValueError, KeyError):
                pass
    if out["mods_on_device"] is not None:
        plan = mod_plan(setup, ctx)
        app_on = {k for k, v in plan["profile"].items() if v} - {m["id"] for m in plan["left_out"]}
        names = {m["id"]: m["name"] for m in plan["included"] + plan["left_out"]}
        device_on = set(out["mods_on_device"])
        out["switched_off"] = sorted(names.get(k, k) for k in device_on - app_on)
        out["switched_on"] = sorted(names.get(k, k) for k in app_on - device_on)
    return out


def pick_device(platform, device, env):
    """the reachable device of PLATFORM named DEVICE (its name or identifier), or the only one"""
    paired = devices(platform, env)
    found = [d for d in paired if d["reachable"] and (not device or device in (d["id"], d["name"]))]
    if len(found) != 1:
        raise Error("%s (%s devices this Mac can reach: %s)"
                    % ("No such device" if device else "Choose a device" if found else "No %s device this Mac can reach" % platform,
                       platform, ", ".join("%s (%s)" % (d["name"], d["id"]) for d in paired if d["reachable"]) or "none"))
    return found[0]


# --------------------------------------------------------------------------------------------- build and install

BUILD_STEPS = [("apple_code", "Translating and compiling the game code"), ("apple_link", "Linking the app"),
               ("apple_files", "Copying the game files"), ("apple_shaders", "Building the shader library"),
               ("apple_icon", "Making the icon"), ("apple_mods", "Building the code mods into the app"),
               ("apple_sign", "Signing the app")]


def build_app(setup, ctx, which, team=None, bundle_prefix="org.wwhdrecomp", signed=True, log=print, step=None):
    """Builds and (signed=False: not, to sign on another Mac) signs the app for WHICH ("tv" / "iphone") into
    data/apple/<tvos|ios>/WindWakerHD.app. step(n, total, title, id): each stage (BUILD_STEPS). Returns built_app()."""
    if which not in KINDS:
        raise Error("--apple-device: tv or iphone")
    step = step or (lambda n, total, title, sid: log(title))
    kind, sdk, platform, suffix = KINDS[which]
    entry = ctx.manifest.get("apple", {}).get(kind)
    if not entry:
        raise Error("This release has no %s app; use a macOS release that includes it" % platform)
    pkg_sdk = os.path.join(setup.PKG, "sdk", "apple", kind)
    dev = developer_dir()
    env = xcode_env(dev)
    sdk_path(env, sdk, platform)
    bundle_id = "%s.%s" % (bundle_prefix, suffix)
    identity = None
    if signed:
        prof_path, prof, identity = find_profile(platform, bundle_id, team)
        log("signing: %s (team %s)" % (prof.get("Name"), prof["TeamIdentifier"][0]))
    steps = [s for s in BUILD_STEPS if signed or s[0] != "apple_sign"]
    n = iter(range(1, len(steps) + 1))
    stage = lambda sid: step(next(n), len(steps), dict(steps)[sid], sid)

    hooks = code_mods_hooks(ctx.data_dir)
    out_dir = Path(ctx.data_dir) / "apple" / kind
    work = out_dir / "work"
    app = out_dir / "WindWakerHD.app"
    shutil.rmtree(setup.longpath(str(work)), ignore_errors=True)
    work.mkdir(parents=True)
    tc = setup.Toolchain(["xcrun", "--sdk", sdk, "clang"], ["xcrun", "--sdk", sdk, "clang++"], ["xcrun", "--sdk", sdk, "ar"],
                         env=env, desc="Xcode " + sdk)
    stage("apple_code")
    log("translating the game code%s" % (" (with code-mod hooks)" if hooks else ""))
    setup.recompile(ctx.game_dir, str(work / "gen"), hooks=hooks, cache_root=ctx.data_dir)
    log("compiling the game code for %s" % platform)
    objs = setup.compile_gamecode(tc, {"gamecode_cflags": entry["gamecode_cflags"]}, str(work / "gen"),
                                  str(work / "obj"), setup.default_jobs(), cache_dir=ctx.data_dir)
    stage("apple_link")
    staging = out_dir / "WindWakerHD.app.partial"
    shutil.rmtree(setup.longpath(str(staging)), ignore_errors=True)
    staging.mkdir(parents=True)
    lib = work / "libgamecode.a"
    run(tc.ar + ["rcs", str(lib)] + objs, env, log, "archiving the game code")
    m = {"sdk": pkg_sdk, "gamecode": str(lib), "out": str(staging / entry["exe"])}
    run(tc.cxx + [setup.sub(a, m) for a in entry["link"]], env, log, "linking the app")
    with open(os.path.join(pkg_sdk, "Info.plist"), "rb") as f:
        info = plistlib.load(f)
    info["CFBundleIdentifier"] = bundle_id
    # what the device's installed app says (device_state): the release and when this app was built
    info["CFBundleShortVersionString"] = ctx.version
    info["CFBundleVersion"] = time.strftime("%Y%m%d.%H%M%S")

    stage("apple_files")
    game = staging / "game"
    game.mkdir()
    for part in ("code", "content", "meta"):
        run(["cp", "-cR", os.path.join(ctx.game_dir, part), str(game / part)], env, log, "copying the game files")
    stage("apple_shaders")
    gen, live = shader_generation(ctx.data_dir, user_dir(setup, ctx))
    if (live / "shaders.bin").is_file():
        shutil.copyfile(live / "shaders.bin", staging / "shaders.bin")
    build_shader_library(setup, ctx, sdk, staging / "shaders.metallib", env, log)
    with open(staging / "Info.plist", "wb") as f:
        plistlib.dump(info, f)
    stage("apple_icon")
    run([sys.executable, apple_tool(setup, "make_icon.py"), str(staging)], env, log, "making the icon")
    stage("apple_mods")
    included = []
    if hooks:
        manager = os.path.join(user_dir(setup, ctx), "ModManager")
        if os.path.isfile(os.path.join(manager, "profiles.json")):
            prepare_mod_addresses(setup, ctx, log)
            out = run([sys.executable, apple_tool(setup, "bundle_mods.py"), "--app", str(staging), "--manager", manager]
                      + (["--sign", identity] if identity else []), env, log, "building the code mods into the app")
            for line in out.splitlines():
                log(line)
            included = sorted(p.stem for p in (staging / "GuestMods").glob("*.dylib"))
    with open(staging / "Info.plist", "rb") as f:
        info = plistlib.load(f)  # with make_icon's entries
    info["WWHDMods"] = included
    with open(staging / "Info.plist", "wb") as f:
        plistlib.dump(info, f)
    if signed:
        stage("apple_sign")
        sign(str(staging), prof_path, prof, identity, bundle_id, env, log)
    shutil.rmtree(setup.longpath(str(app)), ignore_errors=True)
    os.replace(str(staging), str(app))
    shutil.rmtree(setup.longpath(str(work)), ignore_errors=True)
    log("built %s%s" % (app, "" if signed else " (unsigned)"))
    return built_app(ctx, which)


def install_app(setup, ctx, which, device=None, log=print):
    """Installs the app built_app() for WHICH on DEVICE (its name or identifier; the only reachable one when None).
    Refuses while the game runs there, or when that can't be checked. Returns the device."""
    kind, sdk, platform, suffix = KINDS[which]
    built = built_app(ctx, which)
    if not built:
        raise Error("No %s app is built yet" % platform)
    if not built["signed"]:
        raise Error("The app at %s isn't signed: sign it first (an unsigned build is for signing on another Mac)" % built["path"])
    env = xcode_env(developer_dir())
    target = pick_device(platform, device, env)
    exe = ctx.manifest.get("apple", {}).get(kind, {}).get("exe", "WindWakerHD")
    if running(target["id"], exe, env):
        raise Error("The game is running on %s: save, close it (in the app switcher), then install again" % target["name"])
    log("installing on %s (%s)" % (target["name"], target["model"]))
    run(["xcrun", "devicectl", "device", "install", "app", "--device", target["id"], built["path"]], env, log,
        "installing on " + target["name"])
    log("installed on %s: start Wind Waker HD there" % target["name"])
    return target


def build(setup, ctx, which, team=None, device=None, bundle_prefix="org.wwhdrecomp", install=True, signed=True, log=print, step=None):
    """setup.py --apple-device: build_app, then (install and signed) install_app. Returns the app's path."""
    if install and signed:  # (the device first: no build for a device that isn't there)
        pick_device(KINDS[which][2], device, xcode_env(developer_dir()))
    built = build_app(setup, ctx, which, team=team, bundle_prefix=bundle_prefix, signed=signed, log=log, step=step)
    if install and signed:
        install_app(setup, ctx, which, device=device, log=log)
    return built["path"]
