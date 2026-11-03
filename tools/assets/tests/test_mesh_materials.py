import tempfile
import unittest
from pathlib import Path

from tools.assets.mesh_materials import collect, write_new
from tools.assets.resource import Node, Resource


def node(name, **attrs):
    n = Node(name)
    n.attrs.update(attrs)
    return n


class MeshMaterialsTest(unittest.TestCase):
    def test_exact_submesh_and_lod_join(self):
        visual = node("Resource", ID="visual", SourceFile="Meshes\\mesh.gr2")
        visual.children = [node("Objects", ObjectID="name.mesh.2", LOD=1, MaterialID="mat"),
                           node("Objects", ObjectID="name.mesh.0", LOD=0, MaterialID="missing")]
        vb, mb = node("VisualBank"), node("MaterialBank")
        vb.children = [visual]
        mb.children = [node("Resource", ID="mat", SourceFile="Assets/Materials/test.lsf")]
        result = collect([("bank.lsf", Resource([vb, mb], 7))])
        self.assertFalse(result["runtime_join_ready"])
        self.assertEqual(result["rows"][0]["source"], "Meshes/mesh.gr2")
        self.assertEqual(result["rows"][0]["object_id"], "name.mesh.2")
        self.assertEqual(result["rows"][0]["lod"], 1)
        self.assertTrue(result["rows"][0]["material_resolved"])
        self.assertFalse(result["rows"][1]["material_resolved"])

    def test_conflicting_id_retained_as_ambiguous(self):
        bank = node("MaterialBank")
        bank.children = [node("Resource", ID="same", SourceFile="a"), node("Resource", ID="same", SourceFile="b")]
        result = collect([("x", Resource([bank], 7))])
        self.assertEqual(len(result["conflicts"]["MaterialBank:same"]), 2)

    def test_shared_output_is_add_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "db.json"
            write_new(p, {"a": 1})
            before = p.read_bytes()
            with self.assertRaises(FileExistsError):
                write_new(p, {"a": 2})
            self.assertEqual(p.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
