import importlib.util,json,struct,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native_reader',ROOT/'scripts/read-native-output-capture.py')
reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)

class NativeOutputReader(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.path=Path(self.temp.name)
        self.meta={'schema':'nr-native-output-sidecar-v1','present':True,'reason':'none','file':'native-output.nro',
                   'request':1,'instance':2,'frame':3,'feature_generation':4,'recording':5,'reset':0,'queue':7,'fence':8,
                   'source':[9,10,11],'config':12,'evaluation':64,'extent':[3,2],'origin':[1,1],
                   'storage_format':10,'view_format':10,'state_before_copy':8,'native_create_flags':0,'native_result':1,'bytes':48}
        ids=[1,2,3,4,5,0,7,8,9,10,11]
        values=[12,64,3,2,1,1,5,4,10,10,8,24,8,0,1,0]
        self.raw=reader.HEADER.pack(b'NROCAP1\0',1,320,3,1,*ids,*values,1.,1.,48,b'abc',b'fixture',*([0]*8))+bytes(48)
        (self.path/'native-output.nro').write_bytes(self.raw)
        (self.path/'metadata.txt').write_text('format=NRStageCapture-v2\ncomplete=1\nmode=2\nrequest=1\ninstance=2\nframe=3\nfeatureGeneration=4\nconfigGeneration=12\nevaluation=64\nsampledExtent=2x1\n')
        (self.path/'stages.rgba32f').write_bytes(bytes(128));self.write()
    def tearDown(self):self.temp.cleanup()
    def write(self):(self.path/'native-output.json').write_text(json.dumps(self.meta))
    def test_complete(self):
        result=reader.read_capture(self.path);self.assertTrue(result['same_frame_verified']);self.assertFalse(result['photometric_domain_verified'])
    def test_legacy_missing(self):
        (self.path/'native-output.json').unlink();self.assertEqual(reader.read_capture(self.path)['reason'],'legacy-capture-without-O')
    def test_missing_is_explicit(self):
        self.meta.update(present=False,reason='state-unknown',file='');self.write();self.assertFalse(reader.read_capture(self.path)['same_frame_verified'])
    def test_other_frame(self):
        p=self.path/'metadata.txt';p.write_text(p.read_text().replace('frame=3','frame=4'))
        with self.assertRaisesRegex(ValueError,'another parent'):reader.read_capture(self.path)
    def test_mismatched_source(self):
        self.meta['source'][2]+=1;self.write()
        with self.assertRaisesRegex(ValueError,'identity mismatch'):reader.read_capture(self.path)
    def test_unfinished_parent(self):
        p=self.path/'metadata.txt';p.write_text(p.read_text().replace('complete=1','complete=0'))
        with self.assertRaisesRegex(ValueError,'incomplete'):reader.read_capture(self.path)
    def test_short_parent(self):
        (self.path/'stages.rgba32f').write_bytes(bytes(127))
        with self.assertRaisesRegex(ValueError,'incomplete'):reader.read_capture(self.path)
    def test_truncated_o(self):
        (self.path/'native-output.nro').write_bytes(self.raw[:-1])
        with self.assertRaisesRegex(ValueError,'payload'):reader.read_capture(self.path)
    def test_recorded_not_complete(self):
        raw=bytearray(self.raw);struct.pack_into('<I',raw,16,2);(self.path/'native-output.nro').write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'incomplete'):reader.read_capture(self.path)
    def test_no_completion_fence(self):
        raw=bytearray(self.raw);struct.pack_into('<Q',raw,80,0);(self.path/'native-output.nro').write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'completion identity'):reader.read_capture(self.path)

if __name__=='__main__':unittest.main()
