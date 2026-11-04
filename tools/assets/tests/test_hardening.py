"""Malformed input and reproducibility regressions for the offline asset builder."""
import os
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

from tools.assets import build, compress, dbformat, lsf
from tools.assets.extract import effects, materials, probes
from tools.assets.lspk import PackageError, PackageSet
from tools.assets.tests import samples


class LsfBounds(unittest.TestCase):
    def fixture(self):
        raw = bytearray(samples.lsf(('Root', [('A', samples.T_INT, 7), ('B', samples.T_INT, 8)], [])))
        su, _, _, _, nu, _, au, _, _, _ = struct.unpack_from('<10I', raw, 16)
        return raw, 64 + su, 64 + su + nu

    def test_attribute_cycles_and_bounds(self):
        for nxt in (0, 1, 99, -2):
            raw, _, attr = self.fixture()
            # A -> B -> A, self-links and invalid links must all terminate.
            struct.pack_into('<i', raw, attr + (16 if nxt in (0, 1) else 0) + 8, nxt)
            with self.subTest(nxt=nxt), self.assertRaises(lsf.LsfError):
                lsf.parse(raw)

    def test_section_record_name_value_and_parent_bounds(self):
        raw, node, attr = self.fixture()
        variants = []
        for offset, value in ((16, len(raw) * 2), (32, 15), (48, 9999), (60, 99),
                              (64 + 6, 65535), (node, 99999), (node + 4, 0), (attr + 12, 99999)):
            broken = bytearray(raw)
            struct.pack_into('<H' if offset == 70 else '<I', broken, offset, value)
            variants.append(broken)
        variants += [raw[:n] for n in (4, 25, len(raw)-1)]
        for broken in variants:
            with self.subTest(data=broken[:64]), self.assertRaises(lsf.LsfError):
                lsf.parse(broken)

    def test_zero_decoded_size_does_not_bypass_decompression(self):
        raw, node, attr = self.fixture()
        # Strings retain a nonempty stored span but falsely declare zero decoded bytes.
        string_size = struct.unpack_from('<I', raw, 16)[0]
        struct.pack_into('<II', raw, 16, 0, string_size)
        with self.assertRaises(lsf.LsfError):
            lsf.parse(raw)

    def test_negative_format_zero_owner(self):
        raw, node, attr = self.fixture()
        names = raw[64:node]
        nodes = struct.pack('<Iii', 0, 0, -1)
        attrs = struct.pack('<IIi', 1, samples.T_INT | (4 << 6), -1)
        md = struct.pack('<10I', len(names),0,0,0,len(nodes),0,len(attrs),0,4,0)
        data = raw[:16] + md + struct.pack('<BBHI',0,0,0,0) + names + nodes + attrs + struct.pack('<i',7)
        with self.assertRaises(lsf.LsfError):
            lsf.parse(data)


class DecodeSizes(unittest.TestCase):
    def test_methods_reject_wrong_sizes(self):
        plain = b'hello world'
        vectors = [(0, plain, False), (1, zlib.compress(plain), False),
                   (2, samples.lz4_literal_block(plain), False),
                   (2, samples.lz4_frame(samples.lz4_literal_block(plain)), True)]
        for fallback in (False, True):
            with patch.object(compress, '_lz4_block', None if fallback else compress._lz4_block), patch.object(compress, '_lz4_frame', None if fallback else compress._lz4_frame):
                for method, data, frame in vectors:
                    for size in (len(plain)-1, len(plain)+1):
                        with self.subTest(method=method, size=size, fallback=fallback), self.assertRaises(compress.DecompressError):
                            compress.decompress(method, data, size, frame)
                truncated = struct.pack('<IBBBI', compress.LZ4_FRAME_MAGIC, 0x60, 0x40, 0x82, 0x80000020) + b'abc'
                with self.assertRaises(compress.DecompressError):
                    compress.decompress(2, truncated, 32, True)
                with self.assertRaises(compress.DecompressError):
                    compress.decompress(2, b'\xf0', 10)

    def test_optional_frame_decoder_is_bounded(self):
        seen=[]
        class Decoder:
            eof=True
            unused_data=b''
            def decompress(self, data, max_length):
                seen.append(max_length)
                return b'abcd'
        library=SimpleNamespace(LZ4FrameDecompressor=Decoder)
        with patch.object(compress,'_lz4_frame',library):
            self.assertEqual(compress.decompress(2,b'frame',4,True),b'abcd')
            with self.assertRaises(compress.DecompressError):
                compress.decompress(2,b'frame',3,True)
        self.assertEqual(seen,[5,4])


class Packages(unittest.TestCase):
    def test_patch_generation_precedence(self):
        with tempfile.TemporaryDirectory() as directory:
            for name, value in [('Patch8_HotFix30.pak', b'old'), ('Patch9.pak', b'new')]:
                Path(directory, name).write_bytes(samples.lspk({'file': value}))
            self.assertEqual(PackageSet(directory).read('file'), b'new')

    def test_unsupported_or_missing_required_package(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'Required.pak').write_bytes(b'LSPK' + struct.pack('<I', 17) + b'\0'*32)
            with self.assertRaisesRegex(PackageError, 'Required.pak.*v17'):
                PackageSet(directory)
            with self.assertRaisesRegex(PackageError, 'missing'):
                PackageSet(directory, only=['Missing.pak'])

    def test_fingerprint_content_survives_mtime_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, 'Base.pak')
            path.write_bytes(samples.lspk({'file': b'a'}))
            before = PackageSet(directory).fingerprint()[0]
            os.utime(path, (1000,1000))
            after = PackageSet(directory).fingerprint()[0]
            self.assertEqual(before['sha256'], after['sha256'])
            path.write_bytes(samples.lspk({'file': b'b'}))
            self.assertNotEqual(after['sha256'], PackageSet(directory).fingerprint()[0]['sha256'])


class ContainerValidation(unittest.TestCase):
    def fixture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, 'test.bin')
            writer = dbformat.Writer()
            writer.strings.add('valid')
            writer.add_records('TPTS', [(1., 2.)])
            writer.write(path)
            return bytearray(path.read_bytes())

    def test_content_stamp_changes_with_payload(self):
        raw = self.fixture()
        first = dbformat.Reader(raw).build_time
        with tempfile.TemporaryDirectory() as directory:
            writer=dbformat.Writer()
            writer.strings.add('different payload')
            path=Path(directory,'db.bin')
            writer.write(path)
            second=dbformat.Reader(path.read_bytes()).build_time
            writer.write(path)
            self.assertEqual(second,dbformat.Reader(path.read_bytes()).build_time)
        self.assertNotEqual(first,second)

    def test_invalid_toc(self):
        raw = self.fixture()
        toc = dbformat.HEADER.unpack_from(raw)[5]
        for delta, fmt, value in ((4,'I',99),(8,'Q',0),(16,'Q',10000),(24,'I',100),(28,'I',0)):
            broken = bytearray(raw)
            struct.pack_into('<'+fmt, broken, toc+delta, value)
            with self.subTest(delta=delta), self.assertRaises(ValueError):
                dbformat.Reader(broken)
        broken = bytearray(raw)
        # STRS points into TPTS.
        struct.pack_into('<Q',broken,toc+32+8,64)
        with self.assertRaises(ValueError):
            dbformat.Reader(broken)

    def test_string_references_are_local(self):
        reader = dbformat.Reader(self.fixture())
        for ref in (-1,2,99999):
            with self.assertRaises(ValueError):
                reader.string(ref)
        self.assertEqual(reader.string(1),'valid')


class Extraction(unittest.TestCase):
    def test_worker_caches_are_keyed(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            name='Shaders/Vulkan/Test.bshd'
            for directory, stage in ((a,0),(b,4)):
                Path(directory,'Base.pak').write_bytes(samples.lspk({name:samples.bshd(samples.spirv_module(stage)), 'effect.lsfx':directory.encode()}))
            one=materials._hash_worker((a,['Base.pak'],[name]))
            two=materials._hash_worker((b,['Base.pak'],[name]))
            self.assertEqual(one[0][1],'vs')
            self.assertEqual(two[0][1],'fs')
            with patch.object(effects,'parse_effect',side_effect=lambda n,data:[data]):
                self.assertEqual(effects._worker((a,['Base.pak'],['effect.lsfx']))[0],[a.encode()])
                self.assertEqual(effects._worker((b,['Base.pak'],['effect.lsfx']))[0],[b.encode()])

    def test_corrupt_bank_reported_and_required_refs_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory,'Base.pak').write_bytes(samples.lspk({'Public/Test/Content/_merged.lsf':b'broken'}))
            messages=[]
            presets, stats=probes.extract_presets(PackageSet(directory),messages.append)
            self.assertEqual(stats['parse_failures'],1)
            self.assertIn('_merged.lsf',messages[0])
            for worlds, triggers, captures in (([SimpleNamespace(lighting='missing',atmosphere='')],[],[]),
                    ([],[SimpleNamespace(kind='lighting',resources=['missing'],level=0,map_key='trigger')],[]),
                    ([],[],[SimpleNamespace(captures=[SimpleNamespace(lighting='missing')],trigger_lighting='')])):
                with self.assertRaisesRegex(ValueError,'missing'):
                    build._validate_presets(worlds,triggers,captures,{},messages.append)


class DeterministicBuild(unittest.TestCase):
    def test_whole_database_across_jobs_and_mtimes(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            data=root/'game'/'Data'
            data.mkdir(parents=True)
            files={}
            for i, name in enumerate(('Alpha','Beta','Gamma','Delta')):
                stem=f'Public/Shared/Assets/Materials/Test/{name}'
                files[stem+'.lsf']=samples.lsf(('Material',[],[('Texture2DParameters',[('ParameterName',samples.T_FIXED,'Glowmap')],[('StaticDeferred',[('VkDescriptorSet',samples.T_INT,1),('VkBindingIndex',samples.T_INT,2)],[])])]))
                for variant in ('ST_DEF','STI_DEF'):
                    files[stem+'_'+variant+'_Vulkan.bshd']=samples.bshd(samples.spirv_module(4, f'main{i}'))
            pak=data/'Base.pak'
            pak.write_bytes(samples.lspk(files))
            outputs=[]
            with patch.object(build, 'LevelIndex') as index, patch.object(build.lx,'TemplateResolver'), patch.object(build.lx,'extract',return_value=([],{'total':0})), patch.object(build.tx,'trigger_templates'), patch.object(build.tx,'extract',return_value=([],{})), patch.object(build.px,'extract_probes',return_value=([],{})):
                index.return_value.worlds.return_value=[]
                for jobs in (1,2,4):
                    out=root/f'out{jobs}'
                    os.utime(pak,(jobs*1000,jobs*1000))
                    manifest=build.build(str(root/'game'),str(out),jobs=jobs,skip_effects=True,log=lambda message:None)
                    blob=(out/'bg3rtx_assetdb.bin').read_bytes()
                    reader=dbformat.Reader(blob)
                    self.assertGreater(len(reader.rows('MVAR')),0)
                    self.assertGreater(len(reader.rows('SREF')),0)
                    self.assertGreater(len(reader.rows('MPRM')),0)
                    outputs.append(blob)
            self.assertEqual(outputs[0],outputs[1])
            self.assertEqual(outputs[0],outputs[2])
