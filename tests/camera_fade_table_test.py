"""scripts/camera_fade_table.py: the camera-occlusion capsule fade and its per-draw flag.

    py -3 -m unittest tests.camera_fade_table_test
"""
import unittest

from scripts.camera_fade_table import classify

# The shape of 0835ee88b934e3ea (BG3 depth prepass), reduced to what the classifier reads.
FADE = """
layout(set = 0, binding = 6, std140) uniform _18_9
{
    vec3 _m0;
    float _m1;
    vec3 _m2;
    float _m3;
    vec3 _m4;
    float _m5;
} _9;

layout(set = 1, binding = 1, std140) uniform _19_11
{
    vec2 _m0;
    float _m1;
    float _m2;
} _11;

void main()
{
    float _142;
    if (_11._m2 == 1.0)
    {
        float _101 = fma(texture(sampler2D(_13, _10), _4 * vec2(2.0)).x, 2.0, -1.0);
        vec3 _106 = _9._m0 - _9._m4;
        _142 = spvNMin(clamp(spvNMax(fma(_101, 0.5, distance(_84, clamp(_9._m4 + _106, _9._m4, _9._m0))), 0.0) / _9._m1, 0.0, 1.0), clamp(spvNMax(fma(_101, 0.5, distance(_84, _9._m2)), 0.0) / _9._m3, 0.0, 1.0));
    }
    if (fma(_89, _142, -0.2) < 0.0)
    {
        discard;
    }
}
"""
REFL = {"ubos": [{"type": "_18", "set": 0, "binding": 6}, {"type": "_19", "set": 1, "binding": 1}],
        "types": {"_19": {"name": "_19", "members": [
            {"name": "_m0", "type": "vec2", "offset": 0}, {"name": "_m1", "type": "float", "offset": 8},
            {"name": "_m2", "type": "float", "offset": 12}]}}}


class CameraFadeTableTest(unittest.TestCase):
    def test_flag_location(self):
        self.assertEqual(classify(FADE, REFL), (1, 1, 12))

    def test_no_capsule_is_not_fade(self):
        self.assertIsNone(classify(FADE.replace("fma(_101, 0.5, distance(", "fma(_101, 0.5, length("), REFL))

    def test_gate_must_be_a_material_ubo(self):
        # A flag in the frame UBO (set 0) is not the per-draw enable.
        self.assertIsNone(classify(FADE.replace("if (_11._m2 == 1.0)", "if (_9._m5 == 1.0)"), REFL))

    def test_capsule_must_read_the_frame_ubo_segment_only(self):
        self.assertIsNone(classify(FADE.replace("/ _9._m3", "/ _9._m7"), REFL))


if __name__ == "__main__":
    unittest.main()
