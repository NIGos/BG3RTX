import copy
import struct
import unittest
from tools.assets.affine import Affine, UnsupportedAffine
from tools.assets.resource import Node
from tools.assets.probe_foundry_mesh import accessor
from tools.assets.probe_foundry_construction import tile_affine, span_evidence, wall_span_evidence, validate_export, unique, NULL


def node(name,**attrs):
    n=Node(name);n.attrs.update(attrs);return n


def tile(z=88.41464233398438,**overrides):
    t=dict(Translate=[46.15574645996094,.17819547653198242,z],
           Rotate=[-3.5762786865234375e-7,0,-1,0,1,0,1,0,-3.5762786865234375e-7],
           Scale=.800000011920929,ScaleZ=1.,Flip=False,TwoSidedTileCount=0,
           Point1=NULL,Point2=NULL,Stretchable=True,UUID=str(z))
    t.update(overrides);return t


def fixture():
    r=node('GameObjects');ts=node('tiles',ConstructionBend=False,ConstructionPlaceTwoTiles=False)
    spline=node('ConstructionSpline')
    for z in (80.41464233398438,88.41464233398438):
        spline.children.append(node('ConstructionPoint',ConstructionHelperPoint=False,
            ConstructionPointTransform=[-1,0,0,0,0,1,0,0,0,0,-1,0,46.15574645996094,.17819547653198242,z,1]))
    r.children=[ts,spline,node('Transform',Position=[46.15574645996094,.17819547653198242,84.4146728515625],Scale=.8)]
    instances=[]
    for z,length in ((88.41464233398438,4.000000953674316),(85.21463012695312,4.000000953674316),(82.01467895507812,2.)):
        instances.append(dict(tile=tile(z),meshes=[dict(lod=0,bounds=[[-1.220703097715159e-6,0,-1],[length,.14,1]])]))
    instances.append(dict(tile=tile(80.41464233398438,Stretchable=False),meshes=[]))
    return r,instances


class ConstructionProbeTests(unittest.TestCase):
    def test_native_column_major_and_no_parent_pivot(self):
        t=tile();ts=node('tiles',ConstructionBend=False,ConstructionPlaceTwoTiles=False)
        a=tile_affine(t,ts)
        self.assertEqual(a.point((0,0,0)),tuple(t['Translate']))
        # Independent direct column-vector expression from the serialized fields.
        p=(4.,.125,-1.)
        expected=tuple(t['Translate'][i]+t['Scale']*sum(t['Rotate'][j*3+i]*p[j] for j in range(3)) for i in range(3))
        self.assertEqual(a.point(p),expected)
        self.assertLess(a.point((4,0,0))[2],t['Translate'][2])
        # A separate level transform applies once, with no construction pivot.
        self.assertEqual(Affine(translation=(10,20,30)).compose(a).point((0,0,0)),tuple(x+y for x,y in zip(t['Translate'],(10,20,30))))

    def test_selected_native_endpoint_and_unmodified_joint_gaps(self):
        r,instances=fixture();e=span_evidence(r,instances)
        self.assertEqual(e['status'],'AUTHORING_CONSISTENT_NOT_RUNTIME_QUALIFIED')
        self.assertLess(e['signed_joint_gaps'][0],0)
        self.assertGreater(e['signed_joint_gaps'][1],0)
        self.assertNotEqual(e['signed_joint_gaps'][1],0)

    def test_transposed_rotation_rejected_by_independent_endpoints(self):
        r,instances=fixture()
        for n in instances:
            v=n['tile']['Rotate'];n['tile']['Rotate']=[v[j*3+i] for i in range(3) for j in range(3)]
        with self.assertRaises(ValueError):span_evidence(r,instances)

    def test_actual_wall_anchors_keep_authored_visual_overhang(self):
        r=node('GameObjects');ts=node('tiles',ConstructionBend=False,ConstructionPlaceTwoTiles=False)
        spline=node('ConstructionSpline');r.children=[ts,spline]
        for x in (28.76894760131836,41.76894760131836):
            spline.children.append(node('ConstructionPoint',ConstructionHelperPoint=False,
                ConstructionPointTransform=[1,0,0,0,0,1,0,0,0,0,1,0,x,.2703995704650879,93.41004180908203,1]))
        instances=[]
        for x,lo,hi in ((28.76894760131836,-3.0517577442878974e-7,.5884971618652344),
                        (29.76894760131836,-3.0517577442878974e-7,1.003671407699585),
                        (31.76894760131836,-3.0517577442878974e-7,2.003671407699585),
                        (37.76894760131836,-.0036709595005959272,6.003670692443848),
                        (39.76894760131836,-3.0517577442878974e-7,2.003671407699585),
                        (40.76894760131836,-3.0517577442878974e-7,1.003671407699585),
                        (41.76894760131836,-3.0517577442878974e-7,1.003671407699585),
                        (42.26894760131836,-.07847045361995697,.500146746635437)):
            t=tile(Scale=1.,Translate=[x,.2703995704650879,93.41004180908203],UUID=str(x),
                   Rotate=[-1,0,8.742277657347586e-8,0,1,0,-8.742277657347586e-8,0,-1])
            instances.append(dict(tile=t,meshes=[dict(lod=0,bounds=[[lo,0,-.26710978150367737],[hi,4,.018573490902781487]])]))
        original=copy.deepcopy(instances);e=wall_span_evidence(r,instances)
        self.assertTrue(e['endpoint_anchor_identity']);self.assertEqual(instances,original)
        self.assertEqual(len(e['actual_mesh_x_spans']),8)
        # These millimetre-scale visual overhangs are authored geometry, not a
        # floating-point gap to snap away or a proof of continuous opacity.
        self.assertTrue(all(g<0 for g in e['signed_neighbour_mesh_gaps']))
        self.assertLess(min(e['signed_neighbour_mesh_gaps']),-.007)
        instances[3]['tile']['Rotate']=[1,0,0,0,1,0,0,0,1]
        with self.assertRaises(ValueError):wall_span_evidence(r,instances)

    def test_deformation_and_unknown_missing_fields_fail(self):
        ts=node('tiles',ConstructionBend=False,ConstructionPlaceTwoTiles=False)
        for change in ({'ScaleZ':1.0001},{'Flip':True},{'Point1':'linked'},{'Scale':0},{'Scale':float('nan')},{'TwoSidedTileCount':1}):
            with self.assertRaises(UnsupportedAffine):tile_affine(tile(**change),ts)
        bad=tile();del bad['ScaleZ']
        with self.assertRaises(UnsupportedAffine):tile_affine(bad,ts)
        ts.attrs['ConstructionBend']=True
        with self.assertRaises(UnsupportedAffine):tile_affine(tile(),ts)

    def test_resource_identity_is_unambiguous(self):
        n=node('Resource',ID='yes')
        self.assertIs(unique([n],'ID','yes'),n)
        for values in ([],[n,n]):
            with self.assertRaises(ValueError):unique(values,'ID','yes')

    def test_color_unorm_requires_explicit_opt_in(self):
        d={'accessors':[dict(bufferView=0,componentType=5121,type='VEC4',count=1,normalized=True)],
           'bufferViews':[dict(buffer=0,byteLength=4)]}
        with self.assertRaises(ValueError):accessor(d,[bytes((0,128,255,64))],0)
        self.assertEqual(accessor(d,[bytes((0,128,255,64))],0,allow_unorm8=True),[(0,128/255,1,64/255)])

    def test_only_literal_identity_bind_pose_is_accepted(self):
        identity=(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)
        raw=struct.pack('<16f4B4f',*identity,0,0,0,0,1,0,0,0)
        d={'nodes':[{'name':'joint'},{'mesh':0,'skin':0}],
           'skins':[{'joints':[0],'inverseBindMatrices':0}],
           'meshes':[{'primitives':[{'attributes':{'JOINTS_0':1,'WEIGHTS_0':2}}], 'extensions':{'EXT_lslib_profile':{}}}],
           'accessors':[dict(bufferView=0,componentType=5126,type='MAT4',count=1),
                        dict(bufferView=1,componentType=5121,type='VEC4',count=1),
                        dict(bufferView=2,componentType=5126,type='VEC4',count=1)],
           'bufferViews':[dict(buffer=0,byteOffset=0,byteLength=64),dict(buffer=0,byteOffset=64,byteLength=4),dict(buffer=0,byteOffset=68,byteLength=16)]}
        validate_export(d,node('Visual'),[raw])
        changed=bytearray(raw);struct.pack_into('<f',changed,12*4,1)
        with self.assertRaises(UnsupportedAffine):validate_export(d,node('Visual'),[changed])
        changed=bytearray(raw);struct.pack_into('<f',changed,68,.5)
        with self.assertRaises(UnsupportedAffine):validate_export(d,node('Visual'),[changed])
        changed=copy.deepcopy(d);changed['nodes'][0]['translation']=[1,0,0]
        with self.assertRaises(UnsupportedAffine):validate_export(changed,node('Visual'),[raw])


if __name__=='__main__':unittest.main()
