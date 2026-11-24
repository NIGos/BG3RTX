"""tools/live_batches/liveness.py: real lines of the 2026-11-21 queue logs (docs/REGRESSION_PROCESS.md)."""
from pathlib import Path
import json
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import liveness as L

READ_ULTRA = "04:03:50.964  [cfg] settings read: bg3rtx.ini (369 keys) + auto.ini (379 keys, preset=ultra) -> 379 keys"
READ_HIGH = "06:22:52.862  [cfg] settings read: bg3rtx.ini (369 keys) + auto.ini (379 keys, preset=high) -> 379 keys"
DETAILED = "04:03:50.965  [cfg] detailed logging: 1"
# final-s8-ultra-high.1.040347 (before the OMM fix 5a210b12, 04:54)
REJECTED = "04:04:11.990  [omm] rejected ticket use without live ticket/tracked command recording; retaining until idle"
HANDOUT_DEAD = ("{t}  [rt] scene handout: 60 frames, reserved 29, handed out 26, in order 25, fallbacks 0 (not committed 0, "
                "build failed 0, order 0, dropped 0), pairing mismatches 0, committed first 0, unused 0, users refused 0; "
                "OMM accelerated commits 0, downgraded 1; hit material queries at the commit 0, failed 0 (fallbacks: query 0, camera 0)")
# final-s8-ultra-high.1.062059 (after the fix)
HANDOUT_ALIVE = ("{t}  [rt] scene handout: 60 frames, reserved 31, handed out 28, in order 28, fallbacks 0 (not committed 0, "
                 "build failed 0, order 0, dropped 0), pairing mismatches 0, committed first 0, unused 0, users refused 0; "
                 "OMM accelerated commits 32, downgraded 0; hit material queries at the commit 0, failed 0 (fallbacks: query 0, camera 0)")
OMM_DYN = ("{t}  [omm dyn] last 60 builds (rt_omm_dyn 1): masked per-draw draws 7.9 (0.056M triangles) per build, with a "
           "micro-map 7.7 (0.055M triangles, 98.3%); baked 8 micro-maps (0.057M triangles), deferred 0, failed 0, evicted 0; "
           "accelerated scenes 58; cache 8 micro-maps, 0.057M triangles")
SUN_UNKNOWN = ("{t}  [core] sun stats (600 frames, replace 0, 0 roof volumes (level unknown: nothing trusted), scene top 14.4): "
               "traced 76.1% of px, lit 32.9%; game dark & RT lit 0.00% of traced px")
SUN_KNOWN = ("{t}  [core] sun stats (600 frames, replace 1, 153 roof volumes, scene top 14.4): traced 76.1% of px, lit 32.9%; "
             "game dark & RT lit 0.40% of traced px")
# core/gi.cpp pass_timing_collect, kSunResolve (detailed logging): the cost line of the sun resolve pass
# final-stock-hair-dialogue1.1.194918 (2026-11-24, level vote): the menu, then the game level found after a load
LEVEL_MENU = "19:49:46.886  [assetdb] level SYS_Menuscreen_Act3_B (from lights): 78 lights, 4 triggers, 34 probes"
LEVEL_GAME = "{t}  [assetdb] level WLD_Main_A (from lights): 4069 lights, 280 triggers, 1057 probes"
SUN_RESOLVE = "{t}  [core] gi: sun resolve pass GPU cost 0.142 ms/frame (average of 300 dispatched frames)"
MASKS = r"06:21:24.848  [vt alpha] 54 masks loaded from E:\x\data\assetdb\vt_alpha (43.83 MB BC4)"
MASK_INVALID = "06:21:24.518  [vt alpha] 1_1ee1bfca43a33d7e060b496f46d293e9.vtalpha invalid: skipped"


def stamp(start_s, n, step, template):
    out = []
    for i in range(n):
        t = start_s + i * step
        out.append(template.format(t="%02d:%02d:%06.3f" % (t // 3600, t % 3600 // 60, t % 60)))
    return out


class Liveness(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec, cls.rules = L.load_spec()
        cls.profiles = L.Profiles()
        cls.by_id = {r.id: r for r in cls.rules}

    def check(self, lines):
        return L.check_text("\n".join(lines), self.rules, self.profiles)

    def test_dead_omm_log_before_the_fix(self):
        t0 = 4 * 3600 + 4 * 60
        log = [READ_ULTRA, DETAILED, REJECTED] + stamp(t0, 100, 2, HANDOUT_DEAD)
        r = self.check(log)
        self.assertEqual(r["omm_handout_commits"]["ultra"]["status"], L.DEAD)
        self.assertEqual(r["omm_session_latch"]["ultra"]["status"], L.DEAD)
        # the latch hides the [omm dyn] lines: silent with detailed logging is DEAD too
        self.assertEqual(r["omm_dyn_scenes"]["ultra"]["status"], L.DEAD)
        self.assertIn("DEAD omm_handout_commits [ultra]", "\n".join(L.report_lines("x", r)))

    def test_alive_omm_log_after_the_fix(self):
        t0 = 6 * 3600 + 21 * 60
        log = [READ_ULTRA, DETAILED] + stamp(t0, 60, 2, HANDOUT_ALIVE) + stamp(t0 + 1, 60, 2, OMM_DYN)
        r = self.check(log)
        for row in ("omm_handout_commits", "omm_session_latch", "omm_dyn_scenes", "handout_handed_out"):
            self.assertEqual(r[row]["ultra"]["status"], L.ALIVE, row)

    def test_per_preset_ultra_alive_high_dead(self):
        # one log, the preset switched live: liveness is judged per preset, so a dead High is not hidden by Ultra
        t0 = 6 * 3600 + 21 * 60
        log = [READ_ULTRA] + stamp(t0, 20, 2, HANDOUT_ALIVE) + [READ_HIGH] + stamp(t0 + 120, 20, 2, HANDOUT_DEAD)
        r = self.check(log)["omm_handout_commits"]
        self.assertEqual((r["ultra"]["status"], r["high"]["status"]), (L.ALIVE, L.DEAD))

    def test_level_unknown_is_dead_and_a_known_level_is_alive(self):
        t0 = 6 * 3600 + 22 * 60
        r = self.check([READ_ULTRA] + stamp(t0, 15, 10, SUN_UNKNOWN))
        self.assertEqual(r["level_known"]["launch"]["status"], L.DEAD)
        for row in ("sun_replace_active", "roof_volumes"):
            self.assertEqual(r[row]["ultra"]["status"], L.DEAD, row)
        self.assertEqual(r["sun_traced"]["ultra"]["status"], L.ALIVE)
        r = self.check([READ_ULTRA] + stamp(t0, 1, 1, LEVEL_GAME) + stamp(t0, 4, 10, SUN_KNOWN))
        self.assertEqual(r["level_known"]["launch"]["status"], L.ALIVE)
        for row in ("sun_replace_active", "roof_volumes"):
            self.assertEqual(r[row]["ultra"]["status"], L.ALIVE, row)

    def test_level_unknown_during_loads_is_alive_once_the_level_is_found(self):
        # release smoke 2026-11-24: 'level unknown' after each load until the vote found the level; a preset switch
        # after it does not print the level again (one judgement per launch). The menu level alone proves nothing.
        t0 = 6 * 3600 + 22 * 60
        log = ([READ_ULTRA, LEVEL_MENU] + stamp(t0, 12, 2, SUN_UNKNOWN) + stamp(t0 + 25, 1, 1, LEVEL_GAME)
               + stamp(t0 + 30, 5, 10, SUN_KNOWN) + [READ_HIGH] + stamp(t0 + 90, 10, 10, SUN_KNOWN))
        self.assertEqual(self.check(log)["level_known"]["launch"]["status"], L.ALIVE)
        r = self.check([READ_ULTRA, LEVEL_MENU] + stamp(t0, 15, 10, SUN_UNKNOWN))
        self.assertEqual(r["level_known"]["launch"]["status"], L.DEAD)

    def test_sun_resolve_follows_the_level(self):
        # hunt2_claims D3: with the level unknown the resolve pass never runs; with detailed logging its silent cost
        # line is DEAD and names the level rows as its cause. A known level and the cost line: ALIVE.
        t0 = 6 * 3600 + 22 * 60
        r = self.check([READ_ULTRA, DETAILED] + stamp(t0, 15, 10, SUN_UNKNOWN))
        self.assertEqual(r["sun_resolve"]["ultra"]["status"], L.DEAD)
        self.assertEqual(r["sun_resolve"]["ultra"]["follows"], ["level_known", "roof_volumes"])
        self.assertIn("(follows level_known, roof_volumes DEAD)", r["sun_resolve"]["ultra"]["why"])
        r = self.check([READ_ULTRA, DETAILED] + stamp(t0, 15, 10, SUN_KNOWN) + stamp(t0 + 5, 15, 10, SUN_RESOLVE))
        self.assertEqual(r["sun_resolve"]["ultra"]["status"], L.ALIVE)
        self.assertNotIn("follows", r["sun_resolve"]["ultra"])
        # Without detailed logging the pass timers do not print: UNPROVEN, never DEAD.
        r = self.check([READ_ULTRA] + stamp(t0, 15, 10, SUN_UNKNOWN))
        self.assertEqual(r["sun_resolve"]["ultra"]["status"], L.UNPROVEN)
        self.assertTrue(self.by_id["sun_resolve"].line.search(SUN_RESOLVE.format(t="00:00:00.000")))

    def test_rejected_masks_are_dead_once_per_launch(self):
        r = self.check([READ_ULTRA, MASK_INVALID, MASKS])
        self.assertEqual(r["vt_masks_valid"]["launch"]["status"], L.DEAD)
        self.assertEqual(r["vt_masks_loaded"]["launch"]["status"], L.ALIVE)
        r = self.check([READ_ULTRA, MASKS])
        self.assertEqual(r["vt_masks_valid"]["launch"]["status"], L.ALIVE)

    def test_short_or_unrelated_logs_are_unproven_not_dead(self):
        r = self.check([READ_ULTRA] + stamp(4 * 3600, 2, 2, HANDOUT_DEAD))
        self.assertEqual(r["omm_handout_commits"]["ultra"]["status"], L.UNPROVEN)
        self.assertEqual(r["omm_session_latch"]["ultra"]["status"], L.ALIVE)   # the handout line is its proof
        self.assertEqual(r["level_known"]["launch"]["status"], L.UNPROVEN)     # 2 s: too short to judge

    def test_keys_line_turns_a_row_off_and_a_truncated_token_is_ignored(self):
        keys = "04:03:50.965  [cfg] keys: rr=1 rt_omm=0 rt_omm_dyn=0"
        r = self.check([READ_ULTRA, keys] + stamp(4 * 3600 + 300, 10, 2, HANDOUT_DEAD))
        self.assertEqual(r["omm_handout_commits"], {})   # not on: not judged
        long_line = "04:03:50.965  [cfg] keys: " + " ".join("k%d=1" % i for i in range(400)) + " rt_omm=0"
        self.assertGreater(len(long_line), L.KEYS_LINE_LIMIT)
        _, state = L.segment_settings([READ_ULTRA, long_line], self.profiles)
        self.assertEqual(state["rt_omm"], "1")   # the cut last token does not count

    def test_low_preset_does_not_judge_rt_rows(self):
        read_low = READ_ULTRA.replace("preset=ultra", "preset=low")
        r = self.check([read_low] + stamp(4 * 3600, 10, 2, SUN_UNKNOWN))
        self.assertEqual(r["level_known"], {})
        self.assertEqual(r["omm_handout_commits"], {})

    def test_needles_are_literal_substrings_of_real_matches(self):
        samples = [HANDOUT_DEAD.format(t="00:00:00.000"), REJECTED, OMM_DYN.format(t="00:00:00.000"), MASKS, MASK_INVALID,
                   SUN_UNKNOWN.format(t="00:00:00.000"), SUN_KNOWN.format(t="00:00:00.000"), LEVEL_MENU,
                   LEVEL_GAME.format(t="00:00:00.000")]
        for rule in self.rules:
            for line in samples:
                if rule.line.search(line):
                    self.assertIn(rule.needle, line, rule.id)
        self.assertEqual(L._needle(r"a(b|c)d"), "a")
        self.assertEqual(L._needle(r"(x|yy)"), "")
        self.assertEqual(L._needle(r"frames? done"), "frame")
        self.assertEqual(L._needle(r"[abcdef] x{2,3}yz"), "yz")

    def test_rule_every_on_key_has_a_row(self):
        # AGENTS.md: a key whose default or preset value becomes non-zero needs its liveness row in the same branch
        self.assertEqual(L.rule_problems(self.spec, self.profiles), [])

    def test_rule_catches_a_new_on_key(self):
        spec = json.loads(json.dumps(self.spec))
        profiles = L.Profiles()
        profiles.base["rt_new_feature"] = "1"
        profiles._cache.clear()
        problems = L.rule_problems(spec, profiles)
        self.assertTrue(any(p.startswith("rt_new_feature is non-zero") for p in problems), problems)
        spec["exempt"]["rt_new_feature"] = "a parameter, no work of its own"
        self.assertEqual(L.rule_problems(spec, profiles), [])

    def test_water_rows_are_ultra_only(self):
        # 2026-11-21: the release smoke gated water in High, where rt_water_compose is 0
        for rid in ("water_compose", "water_reflections", "water_shadows"):
            self.assertEqual(next(r for r in self.spec["rows"] if r["id"] == rid)["presets"], ["ultra"])

    def test_real_queue_logs_when_present(self):
        logs = ROOT / "captures/queue/logs"
        if not logs.is_dir() and ROOT.parent.name == "worktrees":   # a worktree: the queue logs live in the main checkout
            logs = ROOT.parents[2] / "captures/queue/logs"
        dead, alive = logs / "final-s8-ultra-high.1.040347.log", logs / "final-s8-ultra-high.1.062059.log"
        if not alive.exists():
            self.skipTest("no 2026-11-21 queue logs in this checkout")
        _, _, res = L.run([p for p in (dead, alive) if p.exists()], cache_path=None)
        a = res[str(alive)]
        if dead.exists():   # deleted with the analysed raw logs since
            self.assertEqual(L.log_status(res[str(dead)]["omm_handout_commits"]), L.DEAD)
        self.assertEqual(L.log_status(a["omm_handout_commits"]), L.ALIVE)
        self.assertEqual(a["omm_handout_commits"]["high"]["status"], L.ALIVE)
        self.assertEqual(L.log_status(a["level_known"]), L.DEAD)

    def test_real_level_vote_logs_when_present(self):
        # 2026-11-24 release smoke (level vote on, 12 'level unknown' lines during loads, then WLD_Main_A): ALIVE;
        # 2026-11-21 (before the vote, the level never known): DEAD.
        logs = ROOT / "captures/queue/logs"
        if not logs.is_dir() and ROOT.parent.name == "worktrees":
            logs = ROOT.parents[2] / "captures/queue/logs"
        alive, dead = logs / "final-stock-hair-dialogue1.1.194918.log", logs / "final-s8-ultra-high.1.062059.log"
        if not (dead.exists() and alive.exists()):
            self.skipTest("no 2026-11-21/24 queue logs in this checkout")
        _, _, res = L.run([alive, dead], cache_path=None)
        self.assertEqual(L.log_status(res[str(alive)]["level_known"]), L.ALIVE)
        self.assertEqual(L.log_status(res[str(dead)]["level_known"]), L.DEAD)


if __name__ == "__main__":
    unittest.main()
