#!/usr/bin/env python3
"""Prepare the pinned YuNet model for variable 32-aligned detector references.

Only shape annotations change. Weights and graph operations stay byte-identical.
Requires onnx 1.19.1; emits an explicit hash receipt, never downloads a model.
"""
import argparse
import hashlib
import json
from pathlib import Path
import onnx

SOURCE_SHA = '8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4'

def prepare(source: Path, output: Path):
    if output.exists() or output.with_suffix('.json').exists():
        raise ValueError('Preserve previous model and receipt')
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA:
        raise ValueError('Expected pinned OpenCV Zoo YuNet 2023mar source')
    if onnx.__version__ != '1.19.1':
        raise ValueError('Use the recorded onnx 1.19.1 serializer')
    model = onnx.load_model_from_string(data)
    nodes = [n.SerializeToString() for n in model.graph.node]
    weights = [w.SerializeToString() for w in model.graph.initializer]
    if len(model.graph.input) != 1 or model.graph.input[0].name != 'input':
        raise ValueError('Unexpected model input contract')
    shape = model.graph.input[0].type.tensor_type.shape.dim
    if [d.dim_value for d in shape] != [1, 3, 640, 640]:
        raise ValueError('Unexpected original model shape')
    shape[2].dim_param, shape[3].dim_param = 'height', 'width'
    del model.graph.value_info[:]
    for tensor in model.graph.output:
        tensor.type.tensor_type.shape.dim[1].dim_param = tensor.name + '_cells'
    onnx.checker.check_model(model)
    assert nodes == [n.SerializeToString() for n in model.graph.node]
    assert weights == [w.SerializeToString() for w in model.graph.initializer]
    result = model.SerializeToString()
    output.write_bytes(result)
    receipt = {'schema':'nr-face-model-shape-v1', 'source_sha256':SOURCE_SHA,
               'sha256':hashlib.sha256(result).hexdigest(), 'bytes':len(result),
               'onnx_version':onnx.__version__, 'nodes_weights_unchanged':True,
               'source':'https://github.com/opencv/opencv_zoo/tree/47534e27/models/face_detection_yunet',
               'license':'MIT; distribute the upstream model LICENSE with the prepared model'}
    output.with_suffix('.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    print(json.dumps(receipt))

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();prepare(args.source,args.output)
