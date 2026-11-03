"""Sound declaration-order normalization for the specialization equivalence gate."""
import sys
import concurrent.futures
import unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.spec_cascade_check import (phi_order, block_order, lower_priority, old_requests_waterhair,
                                     old_requests_hairgi, old_requests_haircost, check_baseline, gpu_opt_requests)


class GpuOptRequestsTest(unittest.TestCase):
    def test_off_reference_preserves_every_existing_define(self):
        for request in ("gi/gi_trace_rt.hlsl", "rt/water_compose.hlsl?WATER_LS=1;WATER_SPEC_CASCADE=1"):
            [(assign, reference)] = gpu_opt_requests(request)
            self.assertEqual(assign, {1021: 0, 1022: 0, 1023: 0})
            self.assertEqual(reference, request + (";" if "?" in request else "?") + "BG3RTX_GPU_OPT_LEGACY=1")


class BaselineWorkerTest(unittest.TestCase):
    def test_workers_have_private_reference_paths_and_preserve_assignments(self):
        assign = {1010: 1, 1017: 0}
        with patch("tools.spec_cascade_check.opt", side_effect=lambda tool, src, dst, *args: dst) as optimize, \
                patch("tools.spec_cascade_check.check_one", return_value=0) as compare:
            for identity in ("request_a_hair1", "request_b_hair1"):
                self.assertEqual(check_baseline(None, "scratch", identity, "new", "old", assign, True), 0)
            outputs = [c.args[2] for c in optimize.call_args_list]
            self.assertNotEqual(outputs[0], outputs[1])
            for call in optimize.call_args_list:
                self.assertEqual(call.args[3], "--set-spec-const-default-value=1010:1")
            for call in compare.call_args_list:
                self.assertEqual(call.args[-1], assign)
            optimize.reset_mock()
            check_baseline(None, "scratch", "plain", "new", "old", assign, False)
            optimize.assert_not_called()
            self.assertEqual(compare.call_args.args[-2], "old")


class HairCostRequestsTest(unittest.TestCase):
    def test_all_approximations_cross_hair_presence(self):
        pairs = old_requests_haircost("gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1")
        self.assertEqual([(a[1010], a[1018], a[1019]) for a, _ in pairs],
                         [(h, c, m) for h in (0, 1) for c, m in ((0, 0), (1, 0), (0, 1), (1, 1))])
        for assign, reference in pairs:
            self.assertEqual(assign[1008], 0)
            self.assertEqual(assign[1009], 0)
            self.assertIn(f"RT_HAIR_CHEAP_REFERENCE={assign[1018]}", reference)
            self.assertIn(f"RT_HAIR_GI_MEAN_REFERENCE={assign[1019]}", reference)
            self.assertEqual("RT_NATIVE_HAIR=1" in reference, bool(assign[1010]))

    def test_baseline_reference_does_not_set_new_ids(self):
        assign = {1010: 1, 1018: 0, 1019: 0}
        with patch("tools.spec_cascade_check.opt", side_effect=lambda tool, src, dst, *args: dst) as optimize, \
                patch("tools.spec_cascade_check.check_one", return_value=0):
            self.assertEqual(check_baseline(None, "scratch", "haircost", "new", "old", assign, True), 0)
            self.assertEqual(optimize.call_args.args[3], "--set-spec-const-default-value=1010:1")


class HairGiRequestsTest(unittest.TestCase):
    def test_hair_on_control_and_both_cull_values(self):
        request = "gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1"
        pairs = old_requests_hairgi(request)
        self.assertEqual([(a[1010], a[1017]) for a, _ in pairs], [(0, 0), (0, 1), (1, 0), (1, 1)])
        for assign, reference in pairs:
            self.assertEqual(assign[1008], 0)
            self.assertEqual(assign[1009], 0)
            self.assertIn(f"RT_HAIR_GI_CULL_REFERENCE={assign[1017]}", reference)
            self.assertEqual("RT_NATIVE_HAIR=1" in reference, bool(assign[1010]))
            self.assertNotIn("RT_SPEC_NATIVE", reference)
            self.assertNotIn("RT_SPEC_HAIR_ORIGIN", reference)
            self.assertIn("RT_SPEC_CASCADE=1", reference)

    def test_reference_query_separator_without_other_defines(self):
        pairs = old_requests_hairgi("gi/gi_trace_rt.hlsl?RT_SPEC_NATIVE=1")
        self.assertEqual(pairs[0][1], "gi/gi_trace_rt.hlsl?RT_HAIR_GI_CULL_REFERENCE=0")


class WaterHairRequestsTest(unittest.TestCase):
    def test_existing_native_ids_and_water_layout_defines(self):
        request = "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_SPEC_RF_EDGE=1;RT_SPEC_NATIVE=1;RT_CASCADE_MASKS=1"
        self.assertEqual(old_requests_waterhair(request), [
            ({1008: 0, 1010: 0}, "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_SPEC_RF_EDGE=1;RT_CASCADE_MASKS=1"),
            ({1008: 0, 1010: 1}, "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_SPEC_RF_EDGE=1;RT_NATIVE_SCALP=1;RT_NATIVE_HAIR=1;RT_CASCADE_MASKS=1")])


class WaterHitMappingTest(unittest.TestCase):
    def test_all_modes_keep_descriptor_contract(self):
        from tools.spec_cascade_check import KEYS
        request = "rt/water_compose.hlsl?WATER_HIT_SHADE_LAYOUT=1;WATER_SPEC_CASCADE=1;RT_SPEC_NATIVE=1"
        marker, references = KEYS["waterhit"]
        self.assertEqual(marker, "WATER_HIT_SHADE_LAYOUT=1")
        self.assertEqual(references(request), [
            ({1016: v, 1015: shift}, request + f";WATER_HIT_SHADE_REFERENCE={v};WATER_COMPOSE_SHIFT_REFERENCE={shift}")
            for shift in (0, 1) for v in (0, 1, 2)])


class PhiOrderTest(unittest.TestCase):
    def setUp(self):
        self.types = {"%1": "bool", "%2": "uint"}
        self.a = ["%3 = OpLabel", "%4 = OpPhi %2 %6 %7 %8 %9",
                  "%5 = OpPhi %1 %10 %7 %11 %9", "OpBranchConditional %5 %12 %13"]

    def test_parallel_declarations_only(self):
        other = [self.a[0], self.a[2], self.a[1], self.a[3]]
        self.assertEqual(phi_order(self.a, self.types), phi_order(other, self.types))
        self.assertCountEqual(phi_order(self.a, self.types), self.a)
        self.assertEqual(phi_order(self.a, self.types)[-1], self.a[-1])

    def test_changed_values_edges_ids_and_types_stay_different(self):
        baseline = phi_order(self.a, self.types)
        for old, new in (("%6", "%16"), ("%7", "%17"), ("%4 =", "%14 ="),
                         ("OpPhi %2", "OpPhi %1"), ("%12", "%19")):
            with self.subTest(change=(old, new)):
                changed = [line.replace(old, new) for line in self.a]
                self.assertNotEqual(baseline, phi_order(changed, self.types))

    def test_predecessor_value_pairs_and_block_layout(self):
        first = ["%100 = OpFunction %2 None %101", "%10 = OpLabel",
                 "OpSelectionMerge %40 None", "OpBranchConditional %50 %20 %30",
                 "%20 = OpLabel", "OpBranch %40", "%30 = OpLabel", "OpBranch %40",
                 "%40 = OpLabel", "%4 = OpPhi %2 %6 %20 %8 %30", "OpReturn", "OpFunctionEnd"]
        permuted = first[:4] + first[6:8] + first[4:6] + first[8:]
        permuted[9] = "%4 = OpPhi %2 %8 %30 %6 %20"
        normalize = lambda lines: phi_order(block_order(lines), self.types)
        self.assertEqual(normalize(first), normalize(permuted))
        for replacement in ("%4 = OpPhi %2 %6 %30 %8 %20", "%4 = OpPhi %2 %7 %20 %8 %30"):
            bad = first[:]
            bad[9] = replacement
            self.assertNotEqual(normalize(first), normalize(bad))

    def test_process_workers_preserve_inputs_and_differences(self):
        changed = [line.replace("%6", "%16") for line in self.a]
        with concurrent.futures.ProcessPoolExecutor(2, initializer=lower_priority) as pool:
            a = pool.submit(phi_order, self.a, self.types)
            b = pool.submit(phi_order, changed, self.types)
            self.assertEqual(a.result(), phi_order(self.a, self.types))
            self.assertNotEqual(a.result(), b.result())

    def test_same_type_and_block_boundaries_keep_order(self):
        same = [self.a[0], "%4 = OpPhi %1 %6 %7 %8 %9", self.a[2],
                "%20 = OpLabel", "%21 = OpPhi %2 %22 %23 %24 %25"]
        self.assertEqual(phi_order(same, self.types), same)


if __name__ == "__main__":
    unittest.main()
