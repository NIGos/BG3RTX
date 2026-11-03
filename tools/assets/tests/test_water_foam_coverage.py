import struct
import unittest

import numpy as np

from tools.assets.water_foam_coverage import bc4, mips


class WaterFoamCoverageTests(unittest.TestCase):
    def test_bc4_both_endpoint_orders_and_indices(self):
        for a, b in [(255, 0), (0, 255)]:
            indices = list(range(8))*2
            bits = sum(v << (3*i) for i, v in enumerate(indices))
            block = np.frombuffer(bytes([a, b])+bits.to_bytes(6, 'little'), dtype=np.uint8).reshape(1, 8)
            actual = bc4(block)[0]
            expected = [1, 0, 6/7, 5/7, 4/7, 3/7, 2/7, 1/7] if a > b else [0, 1, 1/5, 2/5, 3/5, 4/5, 0, 1]
            np.testing.assert_allclose(actual, expected*2)

    def test_dxt5_red_and_mip_crop(self):
        header = bytearray(128)
        header[:4] = b'DDS '
        struct.pack_into('<II', header, 12, 4, 4)
        struct.pack_into('<I', header, 28, 3)
        header[84:88] = b'DXT5'
        red = bytes(8)+struct.pack('<HHI', 0xf800, 0, 0)
        black = bytes(16)
        result = mips(bytes(header)+red+black+red)
        self.assertEqual([(x['width'], x['height']) for x in result['mips']], [(4,4),(2,2),(1,1)])
        self.assertEqual([x['channels'][0]['zero_fraction'] for x in result['mips']], [0,1,0])
        self.assertEqual(result['mips'][0]['channels'][0]['min'], 1)

    def test_bc5_independent_channels_and_small_mip(self):
        header = bytearray(128)
        header[:4] = b'DDS '
        struct.pack_into('<II', header, 12, 2, 2)
        struct.pack_into('<I', header, 28, 2)
        header[84:88] = b'BC5U'
        # Every index is zero, so each channel selects its own endpoint 0.
        block = bytes([64, 0])+bytes(6)+bytes([192, 0])+bytes(6)
        result = mips(bytes(header)+block+block)
        self.assertEqual(len(result['mips']), 2)
        for mip in result['mips']:
            self.assertAlmostEqual(mip['channels'][0]['mean'], 64/255)
            self.assertAlmostEqual(mip['channels'][1]['mean'], 192/255)
        with self.assertRaises(ValueError):
            mips(bytes(header)+block+block+b'bad trailing bytes')


if __name__ == '__main__':
    unittest.main()
