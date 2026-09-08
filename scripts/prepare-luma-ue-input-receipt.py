"""Stage a hash-locked external Luma UE source with bounded input logging only.

Never installs an add-on or writes the game/source input. Output is a new external
directory containing patched main.cpp, our header and a private source manifest.
"""
import argparse
import hashlib
import json
from pathlib import Path

COMMIT = '91babfe12abe5f7de0c4890f0efc321d8277887f'
SOURCE_SHA256 = '34a750a7ccbe373b2ab5e8bd690b122c6782c2003e3def864c05edc80fb8a935'
ANCHOR = '               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);'
RECEIPT = '''#if DEVELOPMENT
               nr_luma_receipt::Observe(draw_data, settings_data,
                  nr_luma_receipt::Site{shader_hash, is_compute_shader,
                     taa_shader_info.source_texture_register, taa_shader_info.depth_texture_register,
                     taa_shader_info.velocity_texture_register, taa_shader_info.global_buffer_register_index,
                     global_cb_info.size, global_cb_info.view_to_clip_start_index,
                     global_cb_info.view_size_and_inv_size_index, global_cb_info.clip_to_prev_clip_start_index,
                     bool(game_device_data.camera_cut), dlss_output_changed, native_device_context},
                  [](const char* receipt) { reshade::log::message(reshade::log::level::info, receipt); });
#endif
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    source = a.source.read_bytes()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        ap.error('Not the exact pinned upstream main.cpp bytes; no output written')
    text = source.decode('utf-8').replace('\r\n', '\n')
    include = '#include "includes\\shader_detect.hpp"'
    if text.count(ANCHOR) != 1 or text.count(include) != 1:
        ap.error('Pinned source anchor changed')
    text = text.replace(include, include + '\n#if DEVELOPMENT\n#include "nr-ue-input-receipt.h"\n#endif', 1)
    text = text.replace(ANCHOR, RECEIPT + ANCHOR, 1)
    header = Path(__file__).with_name('luma-ue-input-receipt.h').read_bytes()
    a.output.mkdir(parents=False, exist_ok=False)
    (a.output / 'main.cpp').write_text(text, encoding='utf-8', newline='\n')
    (a.output / 'nr-ue-input-receipt.h').write_bytes(header)
    manifest = {'luma_commit': COMMIT, 'original_main_sha256': SOURCE_SHA256,
                'patched_main_sha256': hashlib.sha256(text.encode()).hexdigest(),
                'receipt_header_sha256': hashlib.sha256(header).hexdigest(),
                'max_rows_per_process': 32, 'game_launched': False,
                'full_addon_compiled': False, 'runtime_inputs_observed': False}
    (a.output / 'source-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print('External diagnostic source staged; no add-on build, install or game execution implied.')


if __name__ == '__main__':
    main()
