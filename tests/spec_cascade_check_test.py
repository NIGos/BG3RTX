"""Sound declaration-order normalization for the specialization equivalence gate."""
import sys
import concurrent.futures
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.spec_cascade_check import phi_order, block_order, lower_priority, old_requests_waterhair


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
