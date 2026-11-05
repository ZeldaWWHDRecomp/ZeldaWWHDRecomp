# SPDX-License-Identifier: MPL-2.0
"""Randomized games made in setup (randomizer_seeds.py) with a stand-in randomizer command: no game files."""
import hashlib
import io
import json
import os
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


class Seeds(unittest.TestCase):
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

    def make(self, **k):
        lines = []
        return rs.make(setup, self.ctx, lines.append, **k), lines

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
        built = []
        rebuild = lambda setup_, c, hooks, *rest: built.append((c.game_dir, hooks)) or {"state": "ready", "cached": False,
                                                                                         "fingerprint": "fp%d" % len(built)}
        with mock.patch("code_mods.rebuild", side_effect=rebuild):
            rs.select(setup, self.ctx, a["id"], True)
            self.assertEqual(built[-1], (str(rs.seed_dir(self.data, a["id"]) / "out"), True))
            self.assertEqual(self.ctx.game_dir, str(self.game))  # (a copy is built from the seed; the install stays)
            self.assertEqual(rs.listing(self.data)["playing"], a["id"])
            self.assertEqual(rs.find(self.data, a["id"])["builds"], {"hooks": "fp1"})
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
            self.assertEqual(rs.load(self.data)["vanilla_builds"], {"plain": "fp2"})
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
