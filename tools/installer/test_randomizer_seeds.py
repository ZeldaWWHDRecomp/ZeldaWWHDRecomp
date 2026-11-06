# SPDX-License-Identifier: MPL-2.0
"""Randomized games made in setup (randomizer_seeds.py) with a stand-in randomizer command: no game files."""
import hashlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup  # noqa: E402
import randomizer_seeds as rs  # noqa: E402

# The stand-in wwhd-rando: the commands setup uses, with the same JSON results; generate writes a randomized
# RPX (bytes from the seed name), the randomizer's metadata, one changed and one unchanged content file and logs.
FAKE = r'''
import json, os, sys
from pathlib import Path
a = sys.argv[1:]
work = Path(a[a.index("--work") + 1]); work.mkdir(parents=True, exist_ok=True)
cmd = [x for i, x in enumerate(a) if i not in (a.index("--data"), a.index("--data") + 1, a.index("--work"), a.index("--work") + 1)]
cfg = work / "config.yaml"
st = json.loads(cfg.read_text()) if cfg.is_file() else {"seed": "Seed0", "preset": "standard"}
def done(**k):
    cfg.write_text(json.dumps(st)); (work / "preferences.yaml").write_text("{}")
    print(json.dumps(dict(ok=True, **k))); sys.exit(0)
def fail(e):
    print(json.dumps(dict(ok=False, error=e))); sys.exit(1)
link = lambda: "LINK-%s-%s" % (st["preset"], st["seed"])
c = cmd[0]
if c == "preset": st["preset"] = cmd[1]; done(preset=cmd[1], permalink=link())
if c == "set-seed": st["seed"] = cmd[1]; done(seed=cmd[1])
if c == "new-seed": st["n"] = st.get("n", 0) + 1; st["seed"] = "Random%d" % st["n"]; done(seed=st["seed"])
if c == "permalink": done(permalink=link())
if c == "load-permalink":
    if cmd[1].startswith("RACE"): fail("Race-mode settings (no spoiler log) are not supported")
    _, st["preset"], st["seed"] = cmd[1].split("-", 2); done(permalink=link())
if c == "locations": done(locations=[])
if c == "settings":
    st.setdefault("mail", 0)
    done(settings={"Progress Mail": {"value": st["mail"], "kind": "bool", "min": 0, "max": 1, "choices": []}},
         seed=st["seed"], permalink=link())
if c == "set":
    if cmd[1] != "Progress Mail": fail("no plain setting named " + cmd[1])
    st["mail"] = int(cmd[2]); done(name=cmd[1], value=st["mail"])
if c == "defaults": st["preset"] = "standard"; st["mail"] = 0; done(seed=st["seed"])
if c == "generate":
    game, out = Path(cmd[cmd.index("--game") + 1]), Path(cmd[cmd.index("--out") + 1])
    for d in ("code", "content/Common", "meta"): (out / d).mkdir(parents=True, exist_ok=True)
    (out / "code/cking.rpx").write_bytes(b"randomized " + link().encode())
    for f in ("code/app.xml", "meta/meta.xml"):
        (out / f).write_text("<app><title_id>0005000010143599</title_id><title_version>0</title_version></app>")
    (out / "content/Common/changed.bin").write_bytes(b"changed")
    (out / "content/Common/same.bin").write_bytes((game / "content/Common/same.bin").read_bytes())
    (work / "logs").mkdir(exist_ok=True)
    log = work / "logs" / ("Hash %s Spoiler Log.txt" % st["seed"]); log.write_text("spoilers")
    (work / "logs" / ("Hash %s Non-Spoiler Log.txt" % st["seed"])).write_text("none")
    done(version="1.2.0", release="USA", hash="Hash " + st["seed"], permalink=link(), seed=st["seed"],
         spoiler_log=str(log), out=str(out))
fail("unknown " + c)
'''


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.fake = root / "fake_rando.py"
        self.fake.write_text(FAKE)
        (root / "rando-data").mkdir()
        self.data = root / "data"
        self.game = self.data / "game"
        for d in ("code", "content/Common", "meta"):
            (self.game / d).mkdir(parents=True)
        (self.game / "code/cking.rpx").write_bytes(b"the game")
        (self.game / "meta/meta.xml").write_text("<menu><title_id>0005000010143500</title_id></menu>")
        (self.game / "content/Common/same.bin").write_bytes(b"same " * 100)
        (self.game / "content/Common/changed.bin").write_bytes(b"original")
        self.env = mock.patch.dict(os.environ, {"WWHD_RANDO_EXE": str(self.fake), "WWHD_RANDO_DATA": str(root / "rando-data")})
        self.env.start()
        self.log = mock.patch.object(setup, "LOG", SimpleNamespace(f=None, write=lambda line: None))
        self.log.start()
        self.ctx = SimpleNamespace(data_dir=str(self.data), game_dir=str(self.game), manifest={"platform": "test"},
                                   args=SimpleNamespace(jobs=1))
        self.compat = []

        def check(path):  # the base game is the USA build; a seed's output passes unless told otherwise
            path = str(path)
            self.compat.append(path)
            if path == str(self.game):
                return SimpleNamespace(canonical=True, name="USA")
            if getattr(self, "refuse", False):
                raise setup.SetupError("This randomized game is not compatible with this port version.")
            return SimpleNamespace(canonical=True, name="USA", randomizer=True)
        self.check = mock.patch.object(setup, "check_game_version", side_effect=check)
        self.check.start()

    def tearDown(self):
        self.check.stop()
        self.log.stop()
        self.env.stop()
        self.tmp.cleanup()

    def official(self, name="official-seed", rpx=b"official randomized code"):
        """an official randomizer output folder: the whole game, its code and metadata randomized, one file changed"""
        out = Path(self.tmp.name) / name
        shutil.copytree(self.game, out)
        (out / "code/cking.rpx").write_bytes(rpx)
        for f in ("code/app.xml", "meta/meta.xml"):
            (out / f).write_text("<app><title_id>0005000010143599</title_id><title_version>0</title_version></app>")
        (out / "content/Common/changed.bin").write_bytes(b"randomized")
        return out

    def make(self, **k):
        lines = []
        return rs.make(setup, self.ctx, lines.append, **k), lines


class Seeds(Base):
    def test_make_registers_trimmed_seed(self):
        entry, lines = self.make(preset="beginner", seed="Alpha")
        rpx = b"randomized LINK-beginner-Alpha"
        self.assertEqual(entry["id"], hashlib.sha256(rpx).hexdigest()[:16])
        self.assertEqual(entry["rpx_sha256"], hashlib.sha256(rpx).hexdigest())
        self.assertEqual((entry["hash"], entry["permalink"], entry["release"]), ("Hash Alpha", "LINK-beginner-Alpha", "USA"))
        d = rs.seed_dir(self.data, entry["id"])
        self.assertTrue((d / "out/code/cking.rpx").is_file())
        self.assertTrue((d / "out/content/Common/changed.bin").is_file())
        self.assertFalse((d / "out/content/Common/same.bin").exists())  # unchanged: left out
        self.assertEqual(entry["spoiler_log"], "logs/Hash Alpha Spoiler Log.txt")
        self.assertTrue((d / entry["spoiler_log"]).is_file())
        self.assertEqual(len(self.compat), 2)  # the base game, then the seed's output (before it is kept)
        self.assertEqual(self.compat[0], str(self.game))
        self.assertTrue(Path(self.compat[1]).parent.name.startswith(".new-") and Path(self.compat[1]).name == "out")
        self.assertEqual([s["id"] for s in rs.load(self.data)["seeds"]], [entry["id"]])
        self.assertFalse([p for p in d.parent.iterdir() if p.name.startswith(".new-")])
        self.assertTrue(any("1 unchanged files left out" in l for l in lines))
        listing = rs.listing(self.data)
        self.assertEqual(listing["playing"], "vanilla")
        self.assertTrue(listing["seeds"][0]["files"])
        self.assertTrue(listing["seeds"][0]["spoiler_log"].endswith("Spoiler Log.txt"))

    def test_same_settings_reuse_the_seed(self):
        first, _ = self.make(seed="Alpha")
        again, lines = self.make(seed="Alpha")
        self.assertEqual(first["id"], again["id"])
        self.assertTrue(any("made %s already" % first["id"] in l for l in lines))
        self.assertEqual(len(rs.load(self.data)["seeds"]), 1)
        other, _ = self.make(new_seed=True)
        self.assertNotEqual(other["id"], first["id"])
        self.assertEqual([s["id"] for s in rs.load(self.data)["seeds"]], [first["id"], other["id"]])
        self.assertEqual(rs.find(self.data, "2")["id"], other["id"])
        self.assertEqual(rs.find(self.data, first["id"][:6])["id"], first["id"])
        self.assertIsNone(rs.find(self.data, "zz"))

    def test_incompatible_seed_leaves_nothing(self):
        self.refuse = True
        with self.assertRaisesRegex(setup.SetupError, "not compatible"):
            self.make(seed="Alpha")
        self.assertEqual(rs.load(self.data)["seeds"], [])
        self.assertEqual(list((self.data / "randomizer/seeds").iterdir()), [])

    def test_refusals(self):
        with self.assertRaisesRegex(setup.SetupError, "Race-mode"):
            self.make(permalink="RACE-x-y")
        self.ctx.manifest = {"variant": "decomp"}
        with self.assertRaisesRegex(setup.SetupError, "decomp"):
            self.make()
        self.ctx.manifest = {}
        (self.game / "meta/meta.xml").write_text("<menu><title_id>0005000010143599</title_id></menu>")
        with self.assertRaisesRegex(setup.SetupError, "randomized one"):
            self.make()

    def test_language_source_note(self):
        with mock.patch.object(setup, "language_sources", return_value=[{"region": "EU"}]):
            _, lines = self.make(seed="Alpha")
        self.assertTrue(any("language source from another region is not used" in l for l in lines))

    def test_select_and_delete(self):
        a, _ = self.make(seed="Alpha")
        b, _ = self.make(seed="Beta")
        # setup's install selected the game as it is (code_mods.remember_installed)
        (self.data / "code-mods-active.json").write_text(json.dumps({"state": "ready", "hooks": False, "fingerprint": "fp0"}))
        built = []
        rebuild = lambda setup_, c, hooks, *rest: built.append((c.game_dir, hooks)) or {"state": "ready", "cached": False,
                                                                                         "fingerprint": "fp%d" % len(built)}
        with mock.patch("code_mods.rebuild", side_effect=rebuild):
            rs.select(setup, self.ctx, a["id"], True)
            self.assertEqual(built[-1], (str(rs.seed_dir(self.data, a["id"]) / "out"), True))
            self.assertEqual(self.ctx.game_dir, str(self.game))  # (a copy is built from the seed; the install stays)
            self.assertEqual(rs.listing(self.data)["playing"], a["id"])
            self.assertEqual(rs.find(self.data, a["id"])["builds"], {"hooks": "fp1"})
            self.assertEqual(rs.load(self.data)["vanilla_builds"], {"plain": "fp0"})  # noted before switching
            with self.assertRaisesRegex(setup.SetupError, "the one the game starts"):
                rs.delete(setup, self.ctx, a["id"])
            saves = self.data / ("save-randomizer-" + b["rpx_sha256"])
            saves.mkdir()
            rs.delete(setup, self.ctx, b["id"])
            self.assertFalse(rs.seed_dir(self.data, b["id"]).exists())
            self.assertTrue(saves.is_dir())  # the player's saves stay
            self.assertEqual([s["id"] for s in rs.load(self.data)["seeds"]], [a["id"]])
            rs.select(setup, self.ctx, "vanilla", False)
            self.assertEqual(built[-1], (str(self.game), False))
            self.assertEqual(rs.listing(self.data)["playing"], "vanilla")
            self.assertEqual(rs.load(self.data)["vanilla_builds"], {"plain": "fp2"})  # (the rebuild's own)
            with self.assertRaisesRegex(setup.SetupError, "no randomized game"):
                rs.select(setup, self.ctx, "ffff", False)

    def test_setup_list_and_delete_need_yes(self):
        a, _ = self.make(seed="Alpha")
        ctx = SimpleNamespace(**vars(self.ctx), installed=lambda: True, state=lambda: {})
        out = io.StringIO()
        with redirect_stdout(out):
            setup.run_randomizer(SimpleNamespace(list_seeds=True), ctx)
        self.assertEqual(json.loads(out.getvalue())["seeds"][0]["id"], a["id"])
        args = SimpleNamespace(list_seeds=False, code_mods=None, delete_seed=a["id"], yes=False, randomize=False, play_seed=None)
        with mock.patch.dict(os.environ, {"WWHD_SETUP_NONINTERACTIVE": "0"}):
            with self.assertRaisesRegex(setup.SetupError, "--yes"):
                setup.run_randomizer(args, ctx)
            args.yes = True
            with mock.patch.object(setup, "say"):
                setup.run_randomizer(args, ctx)
        self.assertEqual(rs.load(self.data)["seeds"], [])

    def test_missing_tool(self):
        with mock.patch.dict(os.environ, {"WWHD_RANDO_EXE": str(Path(self.tmp.name) / "none")}):
            with self.assertRaisesRegex(setup.SetupError, "no randomizer"):
                self.make()


class GuiProtocol(Base):
    """the setup window's Randomizer page: setup.py's rando_* requests"""

    def run_gui(self, *requests):
        from io import StringIO
        ctx = SimpleNamespace(args=SimpleNamespace(code_mods="0", decomp=False, model_sweep=False),
                              manifest={"platform": "test", "toolchain": {}, "version": "test"}, version="test",
                              data_dir=str(self.data), app_dir=str(self.data), game_dir=str(self.game),
                              state=lambda: {"code_mods": False}, installed=lambda: True)
        lines = "".join(json.dumps(dict(r, id=i + 1)) + "\n" for i, r in enumerate(requests))
        out = StringIO()
        built = []
        rebuild = lambda setup_, c, hooks, *rest: built.append(c.game_dir) or {"state": "ready", "fingerprint": "f" * 64}
        with mock.patch.object(setup, "Ctx", return_value=ctx), mock.patch.object(setup, "GUI", None), \
             mock.patch.object(setup, "import_sources", return_value=(None, [])), \
             mock.patch("code_mods.rebuild", side_effect=rebuild), \
             mock.patch.object(setup.sys, "stdin", io.StringIO(lines)), mock.patch.object(setup.sys, "stdout", out):
            self.assertEqual(setup.gui_main(ctx.args), 0)
        replies = [json.loads(l) for l in out.getvalue().splitlines() if json.loads(l).get("event") == "reply"]
        return {r["cmd"] + str(r["id"]): r for r in replies}, built

    def test_page_requests(self):
        r, built = self.run_gui({"cmd": "rando_state"},
                                {"cmd": "rando_change", "set": {"Progress Mail": 1}},
                                {"cmd": "rando_change", "preset": "everything", "seed": "Gamma"},
                                {"cmd": "rando_change", "permalink": "RACE-x"},
                                {"cmd": "rando_make"},
                                {"cmd": "rando_play", "seed": "vanilla"},
                                {"cmd": "rando_change", "set": {"Starting Gear": 1}})
        self.assertTrue(r["rando_state1"]["ok"])
        self.assertEqual(r["rando_state1"]["settings"]["Progress Mail"]["value"], 0)
        self.assertEqual(r["rando_change2"]["settings"]["Progress Mail"]["value"], 1)
        self.assertEqual(r["rando_change3"]["permalink"], "LINK-everything-Gamma")
        self.assertFalse(r["rando_change4"]["ok"])
        self.assertIn("Race-mode", r["rando_change4"]["message"])
        made = r["rando_make5"]
        self.assertTrue(made["ok"])
        self.assertEqual(made["seeds"]["playing"], made["made"])
        self.assertEqual(made["seeds"]["seeds"][0]["hash"], "Hash Gamma")
        self.assertEqual(built[0], str(rs.seed_dir(self.data, made["made"]) / "out"))
        self.assertTrue(r["rando_play6"]["ok"], r["rando_play6"])
        self.assertEqual(r["rando_play6"]["seeds"]["playing"], "vanilla")
        self.assertEqual(built[1], str(self.game))
        self.assertFalse(r["rando_change7"]["ok"])
        r, _ = self.run_gui({"cmd": "rando_delete", "seed": made["made"]})
        self.assertEqual(r["rando_delete1"]["seeds"]["seeds"], [])

    def test_import_seed_button(self):
        out = self.official()
        r, built = self.run_gui({"cmd": "rando_import", "path": str(out)},
                                {"cmd": "rando_import", "path": str(self.game)})
        made = r["rando_import1"]
        self.assertTrue(made["ok"], made)
        self.assertTrue(made["imported"])
        self.assertEqual(made["seeds"]["playing"], made["made"])
        self.assertTrue(made["seeds"]["seeds"][0]["imported"])
        self.assertEqual(built, [str(rs.seed_dir(self.data, made["made"]) / "out")])
        self.assertFalse(r["rando_import2"]["ok"])
        self.assertIn("not a randomized game", r["rando_import2"]["message"])

    def test_unavailable(self):
        with mock.patch.dict(os.environ, {"WWHD_RANDO_EXE": str(Path(self.tmp.name) / "none")}):
            r, _ = self.run_gui({"cmd": "rando_state"})
        self.assertFalse(r["rando_state1"]["ok"])
        self.assertEqual(r["rando_state1"]["problem"], "unavailable")
        self.assertIn("no randomizer", r["rando_state1"]["message"])


class Import(Base):
    """Import seed...: a seed made with the official TWWHD Randomizer (randomizer_seeds.import_seed), the one route for
    such seeds (setup's Choose your game refuses them, test_randomizer_setup.py)"""

    def test_import_keeps_changed_files_checked_on_the_copy(self):
        out = self.official()
        lines = []
        entry = rs.import_seed(setup, self.ctx, str(out), lines.append)
        digest = hashlib.sha256(b"official randomized code").hexdigest()
        self.assertEqual((entry["id"], entry["rpx_sha256"], entry["imported"]), (digest[:16], digest, True))
        self.assertEqual((entry["permalink"], entry["spoiler_log"], entry["release"]), ("", "", "USA"))
        d = rs.seed_dir(self.data, entry["id"]) / "out"
        self.assertEqual((d / "code/cking.rpx").read_bytes(), b"official randomized code")
        self.assertEqual((d / "content/Common/changed.bin").read_bytes(), b"randomized")
        self.assertFalse((d / "content/Common/same.bin").exists())  # the same as the installed game: left out
        self.assertTrue((d / "meta/meta.xml").is_file() and (d / "code/app.xml").is_file())
        # the compatibility check: the installed game, then the copy that is built (not the player's folder)
        self.assertEqual(self.compat[0], str(self.game))
        self.assertTrue(Path(self.compat[1]).parent.name.startswith(".import-") and Path(self.compat[1]).name == "out")
        self.assertFalse([p for p in d.parent.parent.iterdir() if p.name.startswith(".import-")])
        self.assertTrue((out / "content/Common/same.bin").is_file())  # the player's folder is left as it is
        self.assertTrue(any("no longer needed" in l for l in lines))
        listing = rs.listing(self.data)
        self.assertTrue(listing["seeds"][0]["imported"] and listing["seeds"][0]["files"])
        self.assertEqual(listing["seeds"][0]["spoiler_log"], "")

    def test_import_by_rpx_and_again(self):
        out = self.official()
        first = rs.import_seed(setup, self.ctx, str(out / "code/cking.rpx"), lambda l: None)
        lines = []
        again = rs.import_seed(setup, self.ctx, str(out), lines.append)
        self.assertEqual(first["id"], again["id"])
        self.assertTrue(any("imported already" in l for l in lines))
        self.assertEqual(len(rs.load(self.data)["seeds"]), 1)

    def test_import_refusals(self):
        with self.assertRaisesRegex(setup.SetupError, "output folder of the official TWWHD Randomizer"):
            rs.import_seed(setup, self.ctx, str(Path(self.tmp.name) / "none"), lambda l: None)
        with self.assertRaisesRegex(setup.SetupError, "not a randomized game"):
            rs.import_seed(setup, self.ctx, str(self.game), lambda l: None)
        out = self.official()
        self.refuse = True
        with self.assertRaisesRegex(setup.SetupError, "not compatible"):
            rs.import_seed(setup, self.ctx, str(out), lambda l: None)
        self.assertEqual(rs.load(self.data)["seeds"], [])
        self.assertEqual(list((self.data / "randomizer/seeds").iterdir()), [])  # nothing kept
        self.refuse = False
        self.check.stop()
        with mock.patch.object(setup, "check_game_version", return_value=SimpleNamespace(name="EU")):
            with self.assertRaisesRegex(setup.SetupError, "USA games.*European"):
                rs.import_seed(setup, self.ctx, str(out), lambda l: None)
        self.check.start()
        self.ctx.manifest = {"variant": "decomp"}
        with self.assertRaisesRegex(setup.SetupError, "decomp"):
            rs.import_seed(setup, self.ctx, str(out), lambda l: None)

    def test_imported_seed_plays_and_goes_like_a_made_one(self):
        out = self.official()
        entry = rs.import_seed(setup, self.ctx, str(out), lambda l: None)
        built = []
        rebuild = lambda setup_, c, hooks, *rest: built.append(c.game_dir) or {"state": "ready", "fingerprint": "fp"}
        with mock.patch("code_mods.rebuild", side_effect=rebuild):
            rs.select(setup, self.ctx, entry["id"], False)
            self.assertEqual(built, [str(rs.seed_dir(self.data, entry["id"]) / "out")])  # the per-seed build
            self.assertEqual(rs.listing(self.data)["playing"], entry["id"])
            rs.select(setup, self.ctx, "vanilla", False)
            rs.delete(setup, self.ctx, entry["id"])
        self.assertEqual(rs.load(self.data)["seeds"], [])

    def test_seed_installed_the_old_way_keeps_its_saves_when_imported(self):
        """an install made before the Randomizer page took the output folder as the game; its saves are named by the RPX's
        SHA-256, so the same folder imported beside the game as it is plays with them"""
        out = self.official()
        digest = hashlib.sha256(b"official randomized code").hexdigest()
        old = Path(self.tmp.name) / "old-install"
        old.mkdir()
        (old / "install.json").write_text(json.dumps({"randomizer": {"rpx_sha256": digest}}))
        saves = Path(setup.save_user_dir(str(old)))
        self.assertEqual(saves.parent.name, "save-randomizer-" + digest)
        entry = rs.import_seed(setup, self.ctx, str(out), lambda l: None)
        self.assertEqual("save-randomizer-" + entry["rpx_sha256"], saves.parent.name)

    def test_setup_import_seed_option(self):
        out = self.official()
        ctx = SimpleNamespace(**vars(self.ctx), installed=lambda: True, state=lambda: {"code_mods": False})
        args = SimpleNamespace(list_seeds=False, code_mods=None, delete_seed=None, randomize=False, play_seed=None,
                               import_seed=str(out), rando_no_build=False)
        built = []
        rebuild = lambda setup_, c, hooks, *rest: built.append((c.game_dir, hooks)) or {"state": "ready", "fingerprint": "fp"}
        with mock.patch("code_mods.rebuild", side_effect=rebuild), mock.patch.object(setup, "say"):
            setup.run_randomizer(args, ctx)
        entry = rs.load(self.data)["seeds"][0]
        self.assertTrue(entry["imported"])
        self.assertEqual(built, [(str(rs.seed_dir(self.data, entry["id"]) / "out"), False)])
        self.assertEqual(rs.load(self.data)["playing"], entry["id"])


class Trim(unittest.TestCase):
    def test_trim_keeps_identity_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            game, out = Path(tmp) / "g", Path(tmp) / "o"
            for root in (game, out):
                (root / "code").mkdir(parents=True)
                (root / "code/cking.rpx").write_bytes(b"x")
                (root / "code/a.bin").write_bytes(b"same")
            (out / "code/b.bin").write_bytes(b"new")
            self.assertEqual(rs.trim_unchanged(out, game), (1, 4))
            self.assertEqual(sorted(p.name for p in (out / "code").iterdir()), ["b.bin", "cking.rpx"])


if __name__ == "__main__":
    unittest.main()
