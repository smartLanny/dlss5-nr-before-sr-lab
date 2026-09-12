"""Independent Python reader checks against a C++/WARP-written binary frame."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("nr_replay_reader",ROOT/"scripts/nr_replay.py")
reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)

class ReplayReaderTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="nr-replay-reader-")
        self.root=Path(self.temp.name)
        self.raw=Path(os.environ.get("NR_REPLAY_TEST_FRAME",ROOT/"build/replay-tests/captured.nrf")).read_bytes()
        self.header=reader.Header.from_buffer_copy(self.raw[:ctypes.sizeof(reader.Header)])
        self.payload=self.raw[ctypes.sizeof(reader.Header):]
        runtime={"schema":"nr-replay-runtime-v1","identity_scope":"synthetic metadata for reader fixture; no NR execution"}
        for name in ("nr","core","bridge"):runtime[name+"_sha256"]="0"*64;runtime[name+"_bytes"]=1
        (self.root/"runtime.json").write_text(json.dumps(runtime))
        for i in range(16):
            header=reader.Header.from_buffer_copy(bytes(self.header))
            header.index=i;header.evaluation=i+1;header.since_reset=i+1;header.reset=int(i==0);header.frame=42+i
            (self.root/f"frame-{i:02d}.nrf").write_bytes(bytes(header)+self.payload)
    def tearDown(self):self.temp.cleanup()
    def change(self,index,callback):
        path=self.root/f"frame-{index:02d}.nrf";data=path.read_bytes()
        header=reader.Header.from_buffer_copy(data[:ctypes.sizeof(reader.Header)])
        callback(header);path.write_bytes(bytes(header)+data[ctypes.sizeof(reader.Header):])
    def test_cpp_warp_roundtrip(self):
        report=reader.validate_sequence(self.root)
        self.assertTrue(report["complete"]);self.assertEqual(len(report["frames"]),16)
        self.assertEqual(report["frames"][0]["build"],"WARP-fixture-no-NR")
        with reader.Frame(self.root/"frame-00.nrf") as frame:
            plane=frame.plane("proxy")
            self.assertEqual(plane.shape,(5,7,4));self.assertEqual(float(plane[0,0,0]),.5)
            self.assertEqual(float(plane[0,0,1]),1.)
    def test_missing_frame(self):
        (self.root/"frame-07.nrf").unlink()
        with self.assertRaisesRegex(ValueError,"exactly 16"):reader.validate_sequence(self.root)
    def test_truncated_gpu_payload(self):
        path=self.root/"frame-03.nrf";path.write_bytes(path.read_bytes()[:-4])
        with self.assertRaisesRegex(ValueError,"truncated"):reader.validate_sequence(self.root)
    def test_reset_middle_is_not_contiguous(self):
        self.change(6,lambda h:setattr(h,"reset",1))
        with self.assertRaisesRegex(ValueError,"history"):reader.validate_sequence(self.root)
    def test_old_instance_is_not_same_sequence(self):
        self.change(6,lambda h:setattr(h,"instance",2))
        with self.assertRaisesRegex(ValueError,"instance"):reader.validate_sequence(self.root)
    def test_replayed_or_unsubmitted_list_is_invalid(self):
        self.change(6,lambda h:setattr(h,"completion",2))
        with self.assertRaisesRegex(ValueError,"incomplete"):reader.validate_sequence(self.root)
    def test_plane_overlap_and_overflow(self):
        self.change(6,lambda h:setattr(h.planes[2],"offset",0xfffffffffffffff0))
        with self.assertRaisesRegex(ValueError,"plane ranges"):reader.validate_sequence(self.root)
    def test_complete_source_is_required(self):
        self.change(6,lambda h:setattr(h.planes[0],"width",1))
        with self.assertRaises(ValueError):reader.validate_sequence(self.root)

if __name__=="__main__":unittest.main()
