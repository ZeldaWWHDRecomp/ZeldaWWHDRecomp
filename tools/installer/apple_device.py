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
import re
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


def find_profile(platform, bundle_id, team=None, now=None, icloud=None, device=None):
    """The best development profile for the app on PLATFORM ("tvOS" / "iOS"): one of TEAM (any team when None)
    whose App ID covers BUNDLE_ID, not expired, with a certificate whose identity is in the keychain, and (ICLOUD)
    carrying that iCloud container, and (DEVICE: a UDID) listing that device.
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
            if device and not prof.get("ProvisionsAllDevices") and device not in prof.get("ProvisionedDevices", []):
                continue
            if icloud and (icloud not in ents.get("com.apple.developer.icloud-container-identifiers", []) or
                           (platform == "iOS" and icloud not in ents.get("com.apple.developer.ubiquity-container-identifiers", []))):
                continue
            import hashlib
            certs = [hashlib.sha1(c).hexdigest().upper() for c in prof.get("DeveloperCertificates", [])]
            identity = next((c for c in certs if c in identities), None)
            if not identity:
                continue
            exact = pattern == bundle_id
            rank = (exact, expires or now)
            if not best or rank > best[0]:
                best = (rank, path, prof, identity)
    if not best and icloud:
        raise Error("No development profile for %s with the iCloud container %s. In the Apple Developer site (or "
                    "Xcode, Signing & Capabilities), give the App ID %s the iCloud capability (%s) with that "
                    "container, then let Xcode download its profile. Profiles found: %s"
                    % (bundle_id, icloud, bundle_id, "CloudKit and iCloud Documents" if platform == "iOS" else "CloudKit",
                       "; ".join(seen) or "none"))
    if not best:
        raise Error("No development profile for %s apps%s with its certificate in this Mac's keychain. Open Xcode, "
                    "sign in to your Apple Developer account (Settings > Accounts) and let it make the team's "
                    "profile: e.g. build any %s app for your device once. Profiles found: %s"
                    % (platform, " of team " + team if team else "", platform, "; ".join(seen) or "none"))
    return best[1], best[2], best[3]


def entitlements(prof, bundle_id, icloud=None):
    """with ICLOUD: CloudKit (the save's record, cloud_save.h) everywhere, and iCloud Drive (its folder) on an
    iPhone / iPad: an Apple TV app gets no iCloud Drive"""
    team = prof["TeamIdentifier"][0]
    out = {"application-identifier": "%s.%s" % (team, bundle_id), "com.apple.developer.team-identifier": team,
           "get-task-allow": True}
    if icloud:
        drive = "iOS" in prof.get("Platform", [])
        out.update({"com.apple.developer.icloud-container-identifiers": [icloud],
                    "com.apple.developer.icloud-services": ["CloudKit"] + (["CloudDocuments"] if drive else []),
                    "com.apple.developer.icloud-container-environment": "Development"})
        if drive:
            out["com.apple.developer.ubiquity-container-identifiers"] = [icloud]
    return out


def icloud_info(icloud):
    """the Info.plist entry naming the app's iCloud Drive folder (host_tvos.mm reads the container from it)"""
    return {icloud: {"NSUbiquitousContainerIsDocumentScopePublic": True, "NSUbiquitousContainerName": "Wind Waker HD",
                     "NSUbiquitousContainerSupportedFolderLevels": "Any"}}


SETTINGS_DEFAULTS = {"bundle_prefix": "org.wwhdrecomp", "icloud": "", "team": ""}


def settings(ctx, **given):
    """the identifiers the apps are built with: {bundle_prefix, icloud (an iCloud container, "" for none), team};
    GIVEN values (None: unchanged) are kept for next time in data/apple/settings.json"""
    path = os.path.join(ctx.data_dir, "apple", "settings.json")
    try:
        with open(path, encoding="utf-8") as f:
            saved = json.load(f)
    except (OSError, ValueError):
        saved = {}
    out = dict(SETTINGS_DEFAULTS, **{k: v for k, v in saved.items() if k in SETTINGS_DEFAULTS})
    changed = {k: v for k, v in given.items() if v is not None and k in SETTINGS_DEFAULTS and out[k] != v}
    if changed:
        out.update(changed)
        if not re.fullmatch(r"[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+", out["bundle_prefix"]):
            raise Error("The bundle identifier prefix should look like com.example.wwhd")
        if out["icloud"] and not re.fullmatch(r"iCloud\.[A-Za-z0-9.-]+", out["icloud"]):
            raise Error("The iCloud container should look like iCloud.com.example.wwhd")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=1)
    return out


def sign(app, prof_path, prof, identity, bundle_id, env, log, icloud=None):
    shutil.copyfile(prof_path, os.path.join(app, "embedded.mobileprovision"))
    ent = os.path.join(os.path.dirname(app), "entitlements.plist")
    with open(ent, "wb") as f:
        plistlib.dump(entitlements(prof, bundle_id, icloud), f)
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
    the device library comes from the live cache: the models step and play) and its live cache folder"""
    root = Path(user_dir) / "shader-prebuild"
    live = root / "live"
    try:
        active = json.loads((root / "active.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        active = {}
    gen = root / active.get("generation", "")
    if active.get("generation") and (gen / "headstart.bin").is_file():
        return gen, live
    return None, live  # (no template: the build's models step and play record the shaders)


def sweep_models(setup, ctx, log):
    """The models step of shader preparation (model_sweep.py: the game draws every object's models once, hidden, and
    the live cache records their shaders), for this Mac's game when it hasn't run for this game and build. Only the
    player's own game files are used, so it works without the shader template too. Resumes where it stopped; the
    device's library then covers what it recorded."""
    import shader_prebuild
    import model_sweep
    root = Path(user_dir(setup, ctx)) / "shader-prebuild"
    exe = setup_game_exe(setup, ctx)
    template = Path(setup.PKG) / "tools" / "shadercache" / "template.bin"
    try:
        wanted = shader_prebuild.identity(ctx.game_dir, template if template.is_file() else None, setup.PKG, "metal")
    except (OSError, ValueError) as e:
        log("models step skipped: %s" % e)
        return
    pipeline = shader_prebuild.pipeline_identity_of(exe)
    wanted["runtime"] = ("pipeline:" + pipeline) if pipeline else shader_prebuild.file_hash(exe)
    # with the particle pass: every effect of the common and title particle resources (dsweep.cpp), what actors
    # and the world spawn while running; a device can't afford to compile them during play
    wanted["particles"] = True
    ident = model_sweep.identity_of(wanted, "metal")
    if model_sweep.status(root, "metal", ident) == "done":
        return
    log("drawing the game's models once on this Mac, so the device's shader library covers them (the first time "
        "takes a while; it resumes if stopped)")
    env = shader_prebuild.cache_environment(root / "no-generation", ctx.game_dir, "metal")
    shown = [-1]

    def line(text):
        parsed = shader_prebuild.parse_progress(text)
        if parsed and parsed[1]:
            pct = parsed[0] * 100 // parsed[1]
            if pct >= shown[0] + 5:
                shown[0] = pct
                log("models: %d%%" % pct)

    before = os.environ.get("WWHD_SETUP_MODEL_SWEEP_PARTICLES")
    os.environ["WWHD_SETUP_MODEL_SWEEP_PARTICLES"] = "1"
    try:
        result = model_sweep.run(exe, ctx.game_dir, str(root), "metal", ident, env, line=line, log=log,
                                 processes=model_sweep.processes(1))
    finally:
        if before is None:
            os.environ.pop("WWHD_SETUP_MODEL_SWEEP_PARTICLES", None)
        else:
            os.environ["WWHD_SETUP_MODEL_SWEEP_PARTICLES"] = before
    log("models step: %s" % result)


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
            log("no shader template here: the library holds the shaders this Mac's game has recorded (the models "
                "step and play)")
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


def report(setup, ctx, team=None, bundle_prefix="org.wwhdrecomp", icloud=None):
    """Everything that decides whether the apps can be built and installed here, and what they would hold:
    {xcode: {check, version, path}, mac: {game, shaders}, mods: mod_plan(), devices_error,
     apps: {tv|iphone: {label, platform, bundle_id, checks: {release, sdk, shader_compiler, icon, signing},
                        signing: {...}, ready, problems, app: built_app(), devices: [...]}}}"""
    out = {"apps": {}, "settings": {"bundle_prefix": bundle_prefix, "icloud": icloud or "", "team": team or ""}}
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
                        "No shader template: the build draws the game's models on this Mac for the device's shaders")
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
            prof_path, prof, identity = find_profile(platform, bundle_id, team, icloud=icloud)
            names = signing_identities()
            a["signing"] = {"profile": prof.get("Name"), "team": prof["TeamIdentifier"][0],
                            "team_name": prof.get("TeamName", ""), "identity": names.get(identity, identity),
                            "expires": str(prof.get("ExpirationDate", ""))}
            c["signing"] = check(True, "Signing as %s (%s), profile %s until %s" % (
                a["signing"]["identity"], a["signing"]["team_name"] or a["signing"]["team"], a["signing"]["profile"],
                str(prof.get("ExpirationDate", ""))[:10]))
        except Error:
            try:
                found_team = team_of(platform, team)
                c["signing"] = check(True, "The build registers %s%s with your team (%s) through Xcode" % (
                    bundle_id, " and iCloud " + icloud if icloud else "", found_team))
            except Error:
                c["signing"] = check(False, "", "No %s development certificate in this Mac's keychain" % platform,
                                     "Open Xcode, sign in to your Apple Developer account (Settings > Accounts), and "
                                     "build any %s app for your device once so Xcode makes your certificate" % platform)
        needed = [out["xcode"]["check"], game, shaders] + list(c.values())
        a["problems"] = [x["text"] for x in needed if not x["ok"]]
        a["ready"] = not a["problems"]
        out["apps"][which] = a
    return out


def device_state(setup, ctx, which, device, bundle_prefix="org.wwhdrecomp"):
    """One device, before installing on it: {device, running: bool|None, running_error, installed: {version,
    build}|None, others: [{bundle_id, version}], mods_on_device: [ids]|None, switched_off: [names], switched_on: [names]}
    (others: the game there under other identifiers, which an install leaves as they are, saves included; the mods
    an install would switch off or on there: the app's profile replaces the device's when the app's mods change)."""
    kind, sdk, platform, suffix = KINDS[which]
    xc = xcode_info()
    if not xc:
        raise Error("Xcode isn't installed")
    env = xcode_env(xc["developer_dir"])
    target = pick_device(platform, device, env)
    bundle_id = "%s.%s" % (bundle_prefix, suffix)
    exe = ctx.manifest.get("apple", {}).get(kind, {}).get("exe", "WindWakerHD")
    out = {"device": target, "running": None, "installed": None, "others": [], "mods_on_device": None, "switched_off": [],
           "switched_on": []}
    try:
        out["running"] = running(target["id"], exe, env)
    except Error as e:
        out["running_error"] = str(e)
    with tempfile.TemporaryDirectory() as tmp:
        apps = os.path.join(tmp, "apps.json")
        p = subprocess.run(["xcrun", "devicectl", "device", "info", "apps", "--device", target["id"], "--json-output", apps],
                           env=env, capture_output=True, text=True)
        if p.returncode == 0 and os.path.isfile(apps):
            with open(apps) as f:
                listed = json.load(f).get("result", {}).get("apps", [])
            for a in listed:
                if a.get("bundleIdentifier") == bundle_id:
                    out["installed"] = {"version": a.get("version", ""), "build": a.get("bundleVersion", "")}
                elif a.get("name") == "Wind Waker HD":
                    out["others"].append({"bundle_id": a.get("bundleIdentifier", ""), "version": a.get("version", "")})
        if out["installed"]:
            prof = os.path.join(tmp, "profiles.json")
            _copy_from_app(env, target["id"], bundle_id, which, "user/ModManager/profiles.json", prof)
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


# ------------------------------------------------------------------------------------------------------- saves
# The save between this Mac's game and a device's app: the save's user/*.sav files, copied with devicectl into or
# out of the app's container (an app installed by this developer team only). Whatever a copy replaces is backed
# up on the Mac first (data/apple/backups). A device app that keeps its save in iCloud Drive (iPhone, iPad) has it
# in this Mac's iCloud Drive folder too: the copy goes there, and iCloud brings it to the device.

SETTINGS_FILES = ("display.plist", "controls.json")


def mac_save_dir(setup, ctx):
    """the folder this Mac's game keeps its save in: Settings > Saves' choice, else data/save"""
    try:
        with open(os.path.join(user_dir(setup, ctx), "save-folder.json"), encoding="utf-8") as f:
            folder = json.load(f).get("folder", "")
        if folder:
            return Path(folder)
    except (OSError, ValueError):
        pass
    return Path(ctx.data_dir) / "save"


def mac_game_running(setup, ctx):
    exe = os.path.join(ctx.data_dir, "bin", ctx.manifest["exe"])
    return any(subprocess.run(["pgrep", "-f", e], stdout=subprocess.DEVNULL).returncode == 0
               for e in {exe, setup_game_exe(setup, ctx)})


def _copy_from(env, device, bundle_id, source, dest):
    p = subprocess.run(["xcrun", "devicectl", "device", "copy", "from", "--device", device, "--domain-type",
                        "appDataContainer", "--domain-identifier", bundle_id, "--source", source, "--destination",
                        str(dest)], env=env, capture_output=True, text=True)
    return p.returncode == 0 and Path(dest).exists()


def _copy_to(env, device, bundle_id, source, dest):
    p = subprocess.run(["xcrun", "devicectl", "device", "copy", "to", "--device", device, "--domain-type",
                        "appDataContainer", "--domain-identifier", bundle_id, "--source", str(source), "--destination",
                        dest], env=env, capture_output=True, text=True)
    if p.returncode != 0:
        raise Error("Copying %s to the device failed: %s" % (Path(source).name, (p.stderr or p.stdout).strip()[-300:]))


def _sav_files(folder):
    user = Path(folder) / "user"
    return sorted(p for p in user.glob("*.sav") if p.is_file()) if user.is_dir() else []


def _data_dirs(which):
    """Where the device app keeps its settings and saves, in its container (host_tvos.mm host_tvos_data_dir): an
    Apple TV app in Caches, the others in Application Support. After it, where earlier builds kept them (the app
    moves them over at its next start), read when the first has none."""
    return ["Library/Caches"] if which == "tv" else ["Library/Application Support", "Library/Caches"]


def _copy_from_app(env, device, bundle_id, which, path, dest):
    """PATH in the app's data folder (_data_dirs) into DEST: whether there was one"""
    return any(_copy_from(env, device, bundle_id, d + "/" + path, dest) for d in _data_dirs(which))


def _device_target(setup, ctx, which, device, bundle_prefix):
    kind, sdk, platform, suffix = KINDS[which]
    xc = xcode_info()
    if not xc:
        raise Error("Xcode isn't installed")
    env = xcode_env(xc["developer_dir"])
    target = pick_device(platform, device, env)
    return env, target, "%s.%s" % (bundle_prefix, suffix), ctx.manifest.get("apple", {}).get(kind, {}).get("exe", "WindWakerHD")


def _not_running(env, target, exe):
    try:
        if running(target["id"], exe, env):
            raise Error("Wind Waker HD is open on %s: save, quit it there (also from the app switcher), then try again"
                        % target["name"])
    except Error as e:
        if "is open" in str(e):
            raise
        raise Error("%s. Unlock the device and try again." % e)


def backup_device(setup, ctx, which, device=None, bundle_prefix="org.wwhdrecomp", bundle_id=None, log=print):
    """The device app's save and settings (save and user in its data folder, without the shader caches) into
    data/apple/backups/<bundle id>-<time>. Returns that folder."""
    env, target, own_id, exe = _device_target(setup, ctx, which, device, bundle_prefix)
    bundle_id = bundle_id or own_id
    out = Path(ctx.data_dir) / "apple" / "backups" / ("%s-%s" % (bundle_id, time.strftime("%Y%m%d-%H%M%S")))
    out.mkdir(parents=True)
    got = [part for part in ("save", "user") if _copy_from_app(env, target["id"], bundle_id, which, part, out / part)]
    shutil.rmtree(out / "user" / "shader-prebuild", ignore_errors=True)
    if not got:
        shutil.rmtree(out, ignore_errors=True)
        raise Error("Nothing to back up from %s on %s (is the app installed and started once?)" % (bundle_id, target["name"]))
    log("backed up %s from %s to %s" % (" and ".join(got), target["name"], out))
    return out


def _device_icloud_folder(folder):
    """a device's iCloud Drive save folder -> the same folder on this Mac (iCloud Drive brings it across)"""
    marker = "/Mobile Documents/"
    if marker not in folder:
        return None
    return Path.home() / "Library" / "Mobile Documents" / folder.split(marker, 1)[1]


def save_to_device(setup, ctx, which, device=None, bundle_prefix="org.wwhdrecomp", settings=False, from_app=None, log=print):
    """This Mac's save (or, FROM_APP: another app's on the device, e.g. one installed under an earlier identifier)
    into the device app; SETTINGS: also its settings and save states. Refused while the game is open on the device;
    the device's save and settings are backed up first. Returns what was copied."""
    env, target, bundle_id, exe = _device_target(setup, ctx, which, device, bundle_prefix)
    _not_running(env, target, exe)
    data = _data_dirs(which)[0]
    backup = backup_device(setup, ctx, which, device, bundle_prefix, log=log)
    with tempfile.TemporaryDirectory() as tmp:
        if from_app:
            source = backup_device(setup, ctx, which, device, bundle_prefix, bundle_id=from_app, log=log)
            save, settings_dir = source / "save", source / "user"
        else:
            if mac_game_running(setup, ctx):
                raise Error("The game is open on this Mac: quit it first, so its save is complete")
            save, settings_dir = mac_save_dir(setup, ctx), Path(user_dir(setup, ctx))
        files = _sav_files(save)
        if not any(f.name == "cking.sav" for f in files):
            raise Error("No save in %s" % save)
        # the device app's own save folder choice: an iPhone / iPad app with its save in iCloud Drive
        choice = None
        if (backup / "user" / "save-folder.json").is_file():
            try:
                choice = json.loads((backup / "user" / "save-folder.json").read_text(encoding="utf-8")).get("folder")
            except ValueError:
                pass
        drive = _device_icloud_folder(choice) if choice else None
        if drive:
            if (drive / "user").is_dir():
                keep = Path(ctx.data_dir) / "apple" / "backups" / ("icloud-drive-%s" % time.strftime("%Y%m%d-%H%M%S"))
                shutil.copytree(drive / "user", keep / "user")
                log("backed up the iCloud Drive save to %s" % keep)
            (drive / "user").mkdir(parents=True, exist_ok=True)
            for f in files:
                shutil.copy2(f, drive / "user" / f.name)
            log("the save is in iCloud Drive (%s): the device gets it from there" % drive)
        else:
            for f in files:
                _copy_to(env, target["id"], bundle_id, f, data + "/save/user/" + f.name)
            log("copied the save (%d files) to %s" % (len(files), target["name"]))
        copied = ["save"]
        if settings:
            for name in SETTINGS_FILES:
                if (settings_dir / name).is_file():
                    _copy_to(env, target["id"], bundle_id, settings_dir / name, data + "/user/" + name)
                    copied.append(name)
            states = sorted(p for p in (settings_dir / "states").glob("*") if p.is_file()) if (settings_dir / "states").is_dir() else []
            for f in states:
                _copy_to(env, target["id"], bundle_id, f, data + "/user/states/" + f.name)
            if states:
                copied.append("save states")
            log("copied %s to %s" % (", ".join(copied[1:]) or "no settings", target["name"]))
    return copied


def save_from_device(setup, ctx, which, device=None, bundle_prefix="org.wwhdrecomp", log=print):
    """The device app's save into this Mac's save folder (its save backed up first). Refused while the game is open
    on either."""
    env, target, bundle_id, exe = _device_target(setup, ctx, which, device, bundle_prefix)
    _not_running(env, target, exe)
    if mac_game_running(setup, ctx):
        raise Error("The game is open on this Mac: quit it first")
    got = backup_device(setup, ctx, which, device, bundle_prefix, log=log)
    source = got / "save"
    choice = None
    if (got / "user" / "save-folder.json").is_file():
        try:
            choice = json.loads((got / "user" / "save-folder.json").read_text(encoding="utf-8")).get("folder")
        except ValueError:
            pass
    drive = _device_icloud_folder(choice) if choice else None
    if drive:
        source = drive
    files = _sav_files(source)
    if not any(f.name == "cking.sav" for f in files):
        raise Error("No save on %s" % target["name"])
    dest = mac_save_dir(setup, ctx)
    if _sav_files(dest):
        keep = Path(ctx.data_dir) / "apple" / "backups" / ("mac-save-%s" % time.strftime("%Y%m%d-%H%M%S"))
        shutil.copytree(dest / "user", keep / "user")
        log("backed up this Mac's save to %s" % keep)
    (dest / "user").mkdir(parents=True, exist_ok=True)
    for f in _sav_files(dest):
        if not (source / "user" / f.name).is_file():
            f.unlink()
    for f in files:
        shutil.copy2(f, dest / "user" / f.name)
    log("copied the save from %s to %s" % (target["name"], dest))
    return str(dest)


def pick_device(platform, device, env):
    """the reachable device of PLATFORM named DEVICE (its name or identifier), or the only one"""
    paired = devices(platform, env)
    found = [d for d in paired if d["reachable"] and (not device or device in (d["id"], d["name"]))]
    if len(found) != 1:
        raise Error("%s (%s devices this Mac can reach: %s)"
                    % (("Several devices are named %s: choose one by its identifier" % device if found else "No such device")
                       if device else "Choose a device" if found else "No %s device this Mac can reach" % platform,
                       platform, ", ".join("%s (%s)" % (d["name"], d["id"]) for d in paired if d["reachable"]) or "none"))
    return found[0]


# --------------------------------------------------------------------------------------------- build and install

BUILD_STEPS = [("apple_code", "Translating and compiling the game code"), ("apple_link", "Linking the app"),
               ("apple_files", "Copying the game files"), ("apple_shaders", "Building the shader library"),
               ("apple_icon", "Making the icon"), ("apple_mods", "Building the code mods into the app"),
               ("apple_sign", "Signing the app")]


def build_app(setup, ctx, which, team=None, bundle_prefix="org.wwhdrecomp", signed=True, log=print, step=None, icloud=None):
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
        try:
            prof_path, prof, identity = find_profile(platform, bundle_id, team, icloud=icloud)
        except Error:
            # the App ID or its iCloud capability isn't registered yet: Xcode does it
            prof_path, prof, identity = register_with_xcode(platform, bundle_id, None, team, icloud, env, log)
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
    info.pop("NSUbiquitousContainers", None)
    info.pop("WWHDCloudKitContainer", None)
    if icloud:
        info["WWHDCloudKitContainer"] = icloud
        if platform == "iOS":
            info["NSUbiquitousContainers"] = icloud_info(icloud)
    # what the device's installed app says (device_state): the release and when this app was built
    info["CFBundleShortVersionString"] = ctx.version
    info["CFBundleVersion"] = time.strftime("%Y%m%d.%H%M%S")

    stage("apple_files")
    game = staging / "game"
    game.mkdir()
    for part in ("code", "content", "meta"):
        run(["cp", "-cR", os.path.join(ctx.game_dir, part), str(game / part)], env, log, "copying the game files")
    stage("apple_shaders")
    sweep_models(setup, ctx, log)
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
        # the profile again: Xcode may have replaced it during the build (a device or capability registered)
        try:
            prof_path, prof, identity = find_profile(platform, bundle_id, team, icloud=icloud)
        except Error:
            prof_path, prof, identity = register_with_xcode(platform, bundle_id, None, team, icloud, env, log)
        sign(str(staging), prof_path, prof, identity, bundle_id, env, log, icloud=icloud)
    shutil.rmtree(setup.longpath(str(app)), ignore_errors=True)
    os.replace(str(staging), str(app))
    shutil.rmtree(setup.longpath(str(work)), ignore_errors=True)
    log("built %s%s" % (app, "" if signed else " (unsigned)"))
    return built_app(ctx, which)


# A device that isn't in the app's profile yet (a new Apple TV, iPhone or iPad), or an App ID or iCloud container that
# isn't registered: Xcode registers them when it builds for that device with -allowProvisioningUpdates. Setup lets it
# build a throwaway app with the same bundle identifier and entitlements for the device, then takes the profile
# Xcode made.
PBXPROJ = """// !$*UTF8*$!
{
	archiveVersion = 1;
	classes = {
	};
	objectVersion = 77;
	objects = {
		A0000000000000000000000B = {isa = PBXBuildFile; fileRef = A0000000000000000000000F; };
		A0000000000000000000000F = {isa = PBXFileReference; lastKnownFileType = sourcecode.swift; path = main.swift; sourceTree = "<group>"; };
		A0000000000000000000000E = {isa = PBXFileReference; lastKnownFileType = text.plist.entitlements; path = app.entitlements; sourceTree = "<group>"; };
		A0000000000000000000000A = {isa = PBXFileReference; includeInIndex = 0; lastKnownFileType = wrapper.application; path = Probe.app; sourceTree = BUILT_PRODUCTS_DIR; };
		A00000000000000000000001 = {isa = PBXGroup; children = (A0000000000000000000000F, A0000000000000000000000E, A0000000000000000000000A, ); sourceTree = "<group>"; };
		A00000000000000000000002 = {isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = (A0000000000000000000000B, ); runOnlyForDeploymentPostprocessing = 0; };
		A00000000000000000000003 = {isa = PBXNativeTarget; buildConfigurationList = A00000000000000000000005; buildPhases = (A00000000000000000000002, ); buildRules = ( ); dependencies = ( ); name = Probe; productName = Probe; productReference = A0000000000000000000000A; productType = "com.apple.product-type.application"; };
		A00000000000000000000004 = {isa = PBXProject; attributes = {TargetAttributes = {A00000000000000000000003 = {DevelopmentTeam = @TEAM@; ProvisioningStyle = Automatic; }; }; }; buildConfigurationList = A00000000000000000000006; developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en, Base, ); mainGroup = A00000000000000000000001; projectDirPath = ""; projectRoot = ""; targets = (A00000000000000000000003, ); };
		A0000000000000000000000C = {isa = XCBuildConfiguration; buildSettings = {SDKROOT = @SDK@; PRODUCT_NAME = Probe; SWIFT_VERSION = 5.0; @DEPLOY@ = 17.0; }; name = Debug; };
		A0000000000000000000000D = {isa = XCBuildConfiguration; buildSettings = {SDKROOT = @SDK@; PRODUCT_NAME = Probe; CODE_SIGN_STYLE = Automatic; DEVELOPMENT_TEAM = @TEAM@; CODE_SIGN_ENTITLEMENTS = app.entitlements; GENERATE_INFOPLIST_FILE = YES; PRODUCT_BUNDLE_IDENTIFIER = "@BUNDLE@"; TARGETED_DEVICE_FAMILY = "@FAMILY@"; }; name = Debug; };
		A00000000000000000000005 = {isa = XCConfigurationList; buildConfigurations = (A0000000000000000000000D, ); defaultConfigurationName = Debug; };
		A00000000000000000000006 = {isa = XCConfigurationList; buildConfigurations = (A0000000000000000000000C, ); defaultConfigurationName = Debug; };
	};
	rootObject = A00000000000000000000004;
}
"""


def team_of(platform, team=None):
    """the developer team to register with: TEAM, else the team of this Mac's development profiles for PLATFORM"""
    if team:
        return team
    identities = signing_identities()
    for d in PROFILE_DIRS:
        for path in glob.glob(os.path.join(os.path.expanduser(d), "*.mobileprovision")):
            prof = _plist_of_profile(path)
            if prof and platform in prof.get("Platform", []) and prof.get("Entitlements", {}).get("get-task-allow"):
                import hashlib
                if any(hashlib.sha1(c).hexdigest().upper() in identities for c in prof.get("DeveloperCertificates", [])):
                    return prof["TeamIdentifier"][0]
    raise Error("No developer team found: sign in to Xcode (Settings > Accounts) and let it make a certificate")


def register_with_xcode(platform, bundle_id, device=None, team=None, icloud=None, env=None, log=print):
    """Lets Xcode register the App ID BUNDLE_ID with its iCloud container (ICLOUD) and DEVICE (a UDID; None: any
    device of the platform) in the developer team, and download the profile, by building a throwaway app with the
    same identifier and entitlements. Returns the new profile (find_profile)."""
    team = team_of(platform, team)
    sdk = "appletvos" if platform == "tvOS" else "iphoneos"
    with tempfile.TemporaryDirectory() as tmp:
        proj = Path(tmp) / "Probe.xcodeproj"
        proj.mkdir()
        (proj / "project.pbxproj").write_text(
            PBXPROJ.replace("@TEAM@", team).replace("@SDK@", sdk).replace("@BUNDLE@", bundle_id)
                   .replace("@FAMILY@", "3" if platform == "tvOS" else "1,2")
                   .replace("@DEPLOY@", "TVOS_DEPLOYMENT_TARGET" if platform == "tvOS" else "IPHONEOS_DEPLOYMENT_TARGET"),
            encoding="utf-8")
        (Path(tmp) / "main.swift").write_text(
            "import UIKit\nUIApplicationMain(CommandLine.argc, CommandLine.unsafeArgv, nil, nil)\n", encoding="utf-8")
        ents = {}
        if icloud:
            ents = {"com.apple.developer.icloud-container-identifiers": [icloud],
                    "com.apple.developer.icloud-services": ["CloudKit"] + (["CloudDocuments"] if platform == "iOS" else [])}
            if platform == "iOS":
                ents["com.apple.developer.ubiquity-container-identifiers"] = [icloud]
        with open(Path(tmp) / "app.entitlements", "wb") as f:
            plistlib.dump(ents, f)
        log("asking Xcode to register %s%s%s and make its profile (team %s)" % (
            bundle_id, " with iCloud " + icloud if icloud else "", " and the device " + device if device else "", team))
        p = subprocess.run(["xcodebuild", "-project", str(proj), "-scheme", "Probe", "-configuration", "Debug",
                            "-destination", ("id=" + device) if device else "generic/platform=" + platform,
                            "-allowProvisioningUpdates",
                            "-allowProvisioningDeviceRegistration", "-derivedDataPath", str(Path(tmp) / "dd"), "build"],
                           env=env, cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.stdout.decode("utf-8", "replace")
        if p.returncode != 0:
            errors = [l for l in out.splitlines() if "error:" in l][-5:]
            raise Error("Xcode couldn't register the device or make the profile:\n%s" % "\n".join(errors or out.splitlines()[-10:]))
    return find_profile(platform, bundle_id, team, icloud=icloud, device=device)


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
    # a device the app's profile doesn't list yet (a new device): Xcode registers it, and the app is signed again
    # with the profile it makes
    app = Path(built["path"])
    prof = _plist_of_profile(str(app / "embedded.mobileprovision")) or {}
    if not prof.get("ProvisionsAllDevices") and target["id"] not in prof.get("ProvisionedDevices", []):
        with open(app / "Info.plist", "rb") as f:
            info = plistlib.load(f)
        bundle_id, icloud = info["CFBundleIdentifier"], info.get("WWHDCloudKitContainer")
        team = (prof.get("TeamIdentifier") or [None])[0]
        try:
            prof_path, prof, identity = find_profile(platform, bundle_id, team, icloud=icloud, device=target["id"])
        except Error:
            prof_path, prof, identity = register_with_xcode(platform, bundle_id, target["id"], team, icloud, env, log)
        log("signing again with %s (it lists %s)" % (prof.get("Name"), target["name"]))
        sign(str(app), prof_path, prof, identity, bundle_id, env, log, icloud=icloud)
    log("installing on %s (%s)" % (target["name"], target["model"]))
    run(["xcrun", "devicectl", "device", "install", "app", "--device", target["id"], built["path"]], env, log,
        "installing on " + target["name"])
    log("installed on %s: start Wind Waker HD there" % target["name"])
    return target


def build(setup, ctx, which, team=None, device=None, bundle_prefix="org.wwhdrecomp", install=True, signed=True, log=print, step=None,
          icloud=None):
    """setup.py --apple-device: build_app, then (install and signed) install_app. Returns the app's path."""
    if install and signed:  # (the device first: no build for a device that isn't there)
        pick_device(KINDS[which][2], device, xcode_env(developer_dir()))
    built = build_app(setup, ctx, which, team=team, bundle_prefix=bundle_prefix, signed=signed, log=log, step=step,
                      icloud=icloud)
    if install and signed:
        install_app(setup, ctx, which, device=device, log=log)
    return built["path"]
