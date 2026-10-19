"""The installer helper's foundations (setup_steps.py): whole-or-absent outputs, the install's state,
the result file and the exit codes."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import setup_steps as s  # noqa: E402


@s.command("test-ok", "a test command that succeeds", lambda p: p.add_argument("value"))
def _ok(args):
    return {"value": args.value}


@s.command("test-refuse", "a test command the player can put right", lambda p: None)
def _refuse(args):
    raise s.Refusal("failed", what="a refusal")


@s.command("test-fail", "a test command that breaks", lambda p: None)
def _fail(args):
    raise RuntimeError("broken")


class Making(unittest.TestCase):
    def test_whole_on_success(self):
        with tempfile.TemporaryDirectory() as d:
            final = Path(d) / "data" / "out"
            with s.making(final) as work:
                work.mkdir()
                (work / "f").write_text("x")
                self.assertFalse(final.exists())
            self.assertEqual((final / "f").read_text(), "x")
            self.assertFalse(s.partial_name(final).exists())

    def test_absent_on_error(self):
        with tempfile.TemporaryDirectory() as d:
            final = Path(d) / "out"
            with self.assertRaises(RuntimeError):
                with s.making(final) as work:
                    work.mkdir()
                    raise RuntimeError("interrupted")
            self.assertFalse(final.exists())

    def test_replaces_an_older_one_and_a_leftover(self):
        with tempfile.TemporaryDirectory() as d:
            final = Path(d) / "out"
            final.mkdir()
            (final / "old").write_text("1")
            s.partial_name(final).mkdir()  # left by an interrupted run
            with s.making(final) as work:
                self.assertFalse(work.exists())
                work.mkdir()
                (work / "new").write_text("2")
            self.assertEqual(sorted(p.name for p in final.iterdir()), ["new"])
            self.assertEqual(sorted(p.name for p in Path(d).iterdir()), ["out"])


class State(unittest.TestCase):
    def test_rules(self):
        with tempfile.TemporaryDirectory() as d:
            install = Path(d)
            self.assertEqual(s.load_state(install), {})
            state = {}
            inputs = {"rom": "abc"}
            self.assertFalse(s.step_done(state, "data", inputs))
            s.mark_step(state, "data", inputs, folder="data/extracted/abc")
            s.save_state(install, state)
            again = s.load_state(install)
            self.assertTrue(s.step_done(again, "data", inputs))
            self.assertFalse(s.step_done(again, "data", {"rom": "other"}))  # inputs changed: redo
            self.assertFalse(s.step_done(again, "hd", inputs))

    def test_hd_made_is_left_as_it_is(self):
        with tempfile.TemporaryDirectory() as d:
            install = Path(d)
            made = install / "mods" / s.HD_MOD
            state = {}
            inputs = {"1.5+2.5": r"z:\games\KH 1.5", "2.8": r"z:\games\KH 2.8"}
            s.mark_step(state, "hd-menus", inputs, made_by=s.made_by("hd-menus"))
            folders = {"1.5+2.5": "Z:\\Games\\kh 1.5\\", "2.8": r"Z:\Games\KH 2.8"}  # as Windows compares them
            self.assertFalse(s.hd_made(state, "hd-menus", folders, made))  # nothing there: made again
            made.mkdir(parents=True)
            (made / "layouts").mkdir()
            self.assertTrue(s.hd_made(state, "hd-menus", folders, made))
            # made by other tools, or by an installer that did not keep them (0.1.2's): made again
            s.mark_step(state, "hd-menus", inputs, made_by="another installer's tools")
            self.assertFalse(s.hd_made(state, "hd-menus", folders, made))
            s.mark_step(state, "hd-menus", inputs)
            self.assertFalse(s.hd_made(state, "hd-menus", folders, made))
            self.assertNotEqual(s.made_by("hd-menus"), s.made_by("hd-videos"))
            s.mark_step(state, "hd-menus", inputs, made_by=s.made_by("hd-menus"))
            self.assertFalse(s.hd_made(state, "hd-menus", {**folders, "2.8": r"Z:\Other"}, made))
            self.assertFalse(s.hd_made(state, "hd-videos", folders, made))
            # the command: everything asked for already made, so no collection is read
            s.save_state(install, state)
            result = s.build_hd(
                s.argparse.Namespace(
                    install=install,
                    menus=True,
                    videos=False,
                    c1525=Path(folders["1.5+2.5"]),
                    c28=Path(folders["2.8"]),
                    language="en",
                )
            )
            self.assertTrue(result["skipped"])


class HdTools(unittest.TestCase):
    def test_a_failing_tool_leaves_the_others_made(self):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            (folder / "good.py").write_text("print('made')\n", encoding="utf-8")
            (folder / "bad.py").write_text("raise ValueError('zero-size array')\n", encoding="utf-8")
            log = folder / "setup" / s.HD_ERRORS

            def line_of(tool, template):
                return [sys.executable, str(folder / tool), *template]

            pieces = s.run_hd_tools([("bad.py", []), ("good.py", [])], line_of, folder, log, "en")
            self.assertEqual([p["made"] for p in pieces], [False, True])
            self.assertEqual(pieces[0]["error"], "ValueError: zero-size array")
            self.assertIn("Traceback", log.read_text(encoding="utf-8"))  # the whole of what it said, kept
            # none failing: no log left from before
            s.run_hd_tools([("good.py", [])], line_of, folder, log, "en")
            self.assertFalse(log.exists())


class Results(unittest.TestCase):
    def run_command(self, *argv):
        with tempfile.TemporaryDirectory() as d:
            result = Path(d) / "r.json"
            code = s.main([*argv, "--result", str(result), "--language", "es"])
            return code, json.loads(result.read_text(encoding="utf-8"))

    def test_success(self):
        code, result = self.run_command("test-ok", "7")
        self.assertEqual(code, s.OK)
        self.assertEqual(result["value"], "7")
        self.assertTrue(result["ok"])

    def test_refusal(self):
        code, result = self.run_command("test-refuse")
        self.assertEqual(code, s.REFUSED)
        self.assertFalse(result["ok"])
        self.assertIn("a refusal", result["message"])
        self.assertTrue(result["message"].startswith("Algo ha fallado"))

    def test_failure(self):
        code, result = self.run_command("test-fail")
        self.assertEqual(code, s.FAILED)
        self.assertIn("broken", result["message"])
        self.assertIn("RuntimeError", result["trace"])

    def test_options_before_the_command(self):
        with tempfile.TemporaryDirectory() as d:
            result = Path(d) / "r.json"
            code = s.main(["--result", str(result), "test-ok", "1"])
            self.assertEqual(code, s.OK)
            self.assertEqual(json.loads(result.read_text(encoding="utf-8"))["value"], "1")


if __name__ == "__main__":
    unittest.main()
