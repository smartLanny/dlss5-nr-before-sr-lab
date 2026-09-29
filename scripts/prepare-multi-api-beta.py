#!/usr/bin/env python3
"""Stage the 0.5 D and 0.4.7 compatibility integration without changing the checkout.

Requires #131, #132 and the completed #134 merge. Generated-source availability
is not game verification. MFG work is deferred for the current owner test batch.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = 'beta0.5-dline21-uniform3'
DISPLAY_VERSION = '0.5-dline21-uniform3'
CARRIER_NAME = 'dlss5-native-carrier-045-dx11-compat.addon64'
UPSTREAM = '28aed4099b0fe1c207b20b5fee5364c0773c25c2'
PR131 = 'f88adc33d04803982d98336a3d26e7081bb66f9f'
PR132 = '2ab31a17388fb9cfe3c6394397df4cb555b7a172'
PR134 = '145689aee077fa51c54d0b1dba2d79bc38633b2c'
STATE_132 = '5e067f23051cc313a668e91332e82eef9a5bbcf1'
STATE_CURRENT = '036a601f0948f95641fca61218c5404c5c30819c'
PR137 = '7aa6500ee79deca0689ec85faf2bbb8301d181b7'
REQUIRED_ANCESTORS = (PR131, PR132, PR134, PR137, 'fefd447033b21cf0ae979aa53b7dddc1a8a3e20c', 'c6389f7ea2b7cf865cc23a3bf0afb2268678e3cd', '59ab029614df6fad6ea1e95df18a0a986be58962', 'b0fd489cdfa79d271c0015e35b62186abd0efcd5', 'e071c778d10cb4f079857f128aad3093defb7b9e', '80a6379640cebbd70851d9929790ceee160ace57', '6a0ad7684683993328a7a98a9bea83ac76b01a64')
REQUIRED_ANCESTORS += ('ec338d7d9d69230f208b25e4fd457cc79d03c465', 'fa450c9ec2abf2aa48dbdd016b6164626260058b')
REQUIRED_ANCESTORS += ('2b573c2beb2aa6ea1bacbc2ba4f13cdff49df2dc',)
REQUIRED_ANCESTORS += ('7182b3fd195c8cfb633eec5fa758f3826001a69f',)
REQUIRED_ANCESTORS += ('13e4ac958587cb2b4542db9cf749315261ec17c6',)
REQUIRED_ANCESTORS += ('10d624098d8cd062e7eacd7aa54274669ea641a2', 'd7a6def43a58ea6c4b26c553547429a8ef88b735')
INTEGRATION_CORE_PINS = {
    'src/nr_private_sr_budget.h': 'a284d3f657491ca62ff668010ca38faf38271336',
    'src/nr_residency_policy.h': '168f34a3484be936f64e2c3e60f82bf1456c8873',
    'src/nr_pipeline_cache.h': '364fac2ddcb773a3669f7ec18f25d87e22e90895',
    'src/nr_f8_types.h': '4f435ff7a95c1516ee8ee54700dedae123cd2129',
    'src/nr_aux_shader_runtime.cpp': '97422517284ff8c147a4c2ac2232338acf887d70',
    'src/nr_aux_shader_runtime.hpp': '43816a8c011f41b4267989ed0b2d2a6cf1a22d30',
    'scripts/prepare-aux-artifacts.py': '744f7a012a5f389ac287c900ee4c1f0029a9944e',
    'scripts/compile-aux-artifact.cpp': '560e152616ef6d3e191bcd6ccdba7035ae6c993e',
    'src/interop/nr_external_provider_consumer.inl': '802fef3441def2f2e55e6d9098ac3ea4aa79a2a7',
    'src/interop/nr_external_observer_startup.inl': '4294ac22e68862b2587b6ffd7b3cf0418872be41',
    'src/interop/nr_external_provider_feedback.h': '5d24314a5069698eb423312d957ff9b55607f97a',
    'src/interop/nr_external_provider.h': '2f3233ffd69d739a32009415eeaf2cf709791ede',
    'src/interop/nr_external_provider_abi.h': 'dfe5179d9d59dae473d02cc5ea4a87be769c2c40',
    'src/nr_present_mapping.h': 'bb8bda165d5864a1a76727486a1f0cdf5f6f1dda',
    'config/nr_before_sr.ini': '1242f6eb0dfcc016a7f6abfd71e64c9a838f1947',
    'scripts/build.ps1': '83f64d29c32cad71f320da5ec0a4498d12864ce5',
    'scripts/compile-codec-artifact.cpp': '5607678fb1cf888c92ebf8bb869f4002eee1404e',
    'scripts/embed-dual-shaders.py': '0c4c155ca8e51b75bce5ea77bfb0066ba24c1335',
    'scripts/patch-exposure-access.py': '41f6d5f16edc79784d086545be06c0f49d506cd4',
    'scripts/patch-format-access.py': '3da3e0fff4f44f7768d9f5ec1d1a304520f522dc',
    'scripts/patch-post-compat.py': 'c9ac2073481670364c188ce881e0d0157bad0922',
    'scripts/prepare-codec-artifacts.py': 'fae58e7744fff25c4aafedb055b9f36b788bb8c5',
    'src/interop/nr_exposure_access_d3d12.inl': 'bb75d0d5294fd96f20e5419f7a9e8d7a74d9fa39',
    'src/interop/nr_ngx_parameter_abi.h': '0036426de8c9ccb4a9e2b22aaf4d87d3e2e1b038',
    'src/interop/nr_post_area.hlsl': '00e7eecadbb08278fe664c23297d7ca8068776b5',
    'src/interop/nr_post_feedback.inl': '02730a250cf989d7f827a43390184da3c91d2b0c',
    'src/interop/nr_post_settings.inl': 'd06d57623c1cd13bb91f3c74c13c620aa449a8ba',
    'src/interop/nr_post_ui.inl': 'db28f5ea6a2e6db6beaee409d8429157d3c8e683',
    'src/interop/nr_rr_depth_evidence.inl': '0746e827d6b72b35e26e778b865a9cfdd1587f9e',
    'src/interop/nr_rr_post_runtime.inl': 'b3cd0a80645a0f5e434d73e7d881db1d3c2f1dd5',
    'src/nr_before_sr.cpp': 'b87b5dacb735e448bc4747ecfc30111383a1c980',
    'src/nr_before_sr.rc': '37a8370cbd6cdabb81a7ab7dc17b38e71edc623a',
    'src/nr_binding_runtime.inl': 'b546021cae08144654fd1efb986d916a46414a22',
    'src/nr_binding_types.inl': '2c78122bdbdff885c6f829cad929cb2825377d7a',
    'src/nr_codec_descriptors.inl': 'd05ff8f9656b9a9803be0bf6fb0bf2d4eccbe0e2',
    'src/nr_color_amplification.h': 'ec62c723a9ca88424309a795027e737900e8f587',
    'src/nr_color_policy.inl': '7388056586a4fee6ab333c4979793bcd64be243a',
    'src/nr_command_creation_observer.inl': 'fd5d0f4adf50cb1ea3becb6fa9e51f61bf79c828',
    'src/nr_command_state.inl': '036a601f0948f95641fca61218c5404c5c30819c',
    'src/nr_compatibility_runtime.inl': '92e8f7c8b32c06c3de5aa8772d52077a4271af31',
    'src/nr_compatibility_verdict.h': 'd49019e6078e4237ff65207f56f5dca3fda0048e',
    'src/nr_compatibility_verdict_feedback.inl': 'e2d233a67c8a7aa6e3aefc99cce596e57e730da3',
    'src/nr_config_policy.h': '936883dabf13e79058865075e6cf0bbe507c4dbd',
    'src/nr_config_policy_runtime.inl': 'ef49832b8cb852458aa85f60ac86381c334ae45b',
    'src/nr_descriptor_plan.h': 'c29345e306d8dbf6f897b02cadfd7c75b45cdff6',
    'src/nr_descriptor_provenance.h': '8273ed6d31b18848a3ffd3e1f3da105407db0838',
    'src/nr_descriptor_provenance_commands.inl': '9bcbac6d7f6a2b601fe7b4fc71ee936b0b994bda',
    'src/nr_descriptor_provenance_hooks.inl': '4a67d545785f527380ca4c782b40cb5c9ee21fce',
    'src/nr_descriptor_provenance_runtime.inl': '9907ec0437b6384bdb5294a817f47688a7250e3d',
    'src/nr_diagnostic_bundle.inl': '3eea8e899aef4f54ab0da40ac758c2afe0a0c10b',
    'src/nr_dual_capture.inl': '78ce4dc9e360f99eef9d2e999b56874df03a3ba0',
    'src/nr_dual_codec.inl': 'd1c1b906c72b9aad2001d97b8841e3d1547c82b4',
    'src/nr_dual_downsample.hlsl': 'd930d3c881f6bbc940a30ab000a638d799ac2287',
    'src/nr_dual_embedded.hpp': '0eb3836e79802d42e9d62136f779ae11388d6107',
    'src/nr_dual_lifetime.hpp': '33119e1c92269636feb757eaa5073164ac8d5a8b',
    'src/nr_dual_math.hlsli': '252fa37b70640f31b47828170b666240cfc630fe',
    'src/nr_dual_observation.hpp': 'a5ae692324f1c6ce33c22a5aa8996b21993bbe97',
    'src/nr_dual_pass.hpp': 'd07a10e5361dfd058ac94a932618018466dc6d22',
    'src/nr_dual_residual.hlsl': '545307f5dff6c461ce6830e09cc35695b4861cd0',
    'src/nr_dual_resolve.hlsli': 'dfd746ca6bcc7faab5a2f22c224dcaa26ed3781d',
    'src/nr_dual_resolve_cached.hlsli': 'fce4454d5039019237f4cfe08d5db7c2a8fa92a5',
    'src/nr_dual_runtime.inl': '1c666a8e9bfd31fe9aeb43eede7c56c49ce32929',
    'src/nr_dual_spatial_d3d12.cpp': 'cd4cc38093f13790f11b98e3c7f2297bfcb2095d',
    'src/nr_dual_spatial_d3d12.hpp': '918621d23ec5623c7c6cb892c421c9c71633496e',
    'src/nr_dual_status_runtime.inl': '5e6403b12b13388270f5e7c3304bea50542b9ee6',
    'src/nr_dual_timing.inl': '5f4b1ce2d44fa987f32a99f57fad6eb39a8955c8',
    'src/nr_dual_ui.inl': 'e9cfb7e7b2f8a6cea7b298777a3e721c8ffbd58a',
    'src/nr_exposure_semantic_evidence.inl': '405c6e41036bfcc9ac088541cbc9e61831ad7a13',
    'src/nr_f8_gpu.inl': '4f71892f4d3586949e85f9ca80b75535e4854f6f',
    'src/nr_f8_sequence.inl': '5944fb98c2f81a71846a216246e23ad07d24ab2b',
    'src/nr_f8_stream.inl': 'f9791f8c298f87fa7aefc0de1a42454d2300e616',
    'src/nr_face_capture_geometry.h': '52204086ba8064ca655c075763af7d6cfe0ba4f1',
    'src/nr_face_detector.cpp': '3f03311705df5400d9a893732eaf4c3beea27130',
    'src/nr_face_detector.h': 'e1bfc1c4e82fba6865cf33047761eddfbbb42356',
    'src/nr_face_diagnostic.h': '40894d16ccf00fdd64dc2ab351d40b4043dcb858',
    'src/nr_face_diagnostic_ui.inl': '2c9b5a34f2a5a821cc738c2cad4b3e13f194a67d',
    'src/nr_face_region_selection.inl': 'aabd0d3e6e2d5044f0d16f67b9fc30516a8d5e91',
    'src/nr_face_runtime.inl': 'fad6a74d1018f259685ddd87c1e1f94f4dd09946',
    'src/nr_face_tracker_gpu.inl': '48bce7fe01838387d319b943451cedfc99bc3c10',
    'src/nr_face_tracker_shader.h': '243cb1dae233f045b37951633a67cf3c1b2fb026',
    'src/nr_face_worker.cpp': 'acc6a4c79bb9bbbb8a4212874bf802d7ca5f4184',
    'src/nr_face_worker.h': '29ac5085dee8a9a4715838a6f037c9d3859f89f1',
    'src/nr_feedback_capture.inl': '87d824425c61e6847cd199931c96558270bf0fb8',
    'src/nr_feedback_core.inl': '12a4a40598353f1673b8e4d539ab7fe6b19ae097',
    'src/nr_feedback_limits.h': '80804056007f274e7589cd7de160f0ea5f0aef0e',
    'src/nr_feedback_picture.inl': '961d74f37483276b6b141adcaa4d8345b7c3d22b',
    'src/nr_feedback_report.inl': '07ac914f64d1361cda0d1b4301077247d05bccfa',
    'src/nr_feedback_single_text.h': '2cffa33d926fa3846d4ec3170aa0a7257a962ddf',
    'src/nr_feedback_text.h': '597dc9b3fa4abbd4d694fb42117a125df84bf1b9',
    'src/nr_generic_residual.h': '98244f16f94fe45de1e6226d5538ff00a96b616f',
    'src/nr_guide_contract.h': 'e99524ac44d71ab99365d3b53e2770eaefede25c',
    'src/nr_imgui_abi.h': 'd52148f7dc9999c8090947243b9fe6167f457240',
    'src/nr_light_style.h': '51da9727af98eacb24e309aab9de25eb6ea9f974',
    'src/nr_light_style_ui.inl': '4dba5dc4891db8203433ac7ea354d819b68a9f3a',
    'src/nr_model_controls.h': 'efb975048e790773a981ff0e52f244e828cc75ad',
    'src/nr_motion_slot_selection.h': '5d0e46d101f215de144b36c739d83d8e1a28003a',
    'src/nr_native_clear_request.h': 'f3c68953f94cfbe339d41fb9f0cea189c2d6eb0e',
    'src/nr_native_clear_request_runtime.inl': '941b43e88203bb96866b7f918df87dcf7cde1466',
    'src/nr_native_command_record.inl': '91b4cf6b2c77396b464b2e28ae0093efd5558a68',
    'src/nr_native_format.h': '3eeb9ce944be64be45369df4ea200975d8567800',
    'src/nr_native_format_hooks.inl': '6a12656d6a846715b1d79b229ab17f5323688b97',
    'src/nr_native_format_runtime.inl': '8462e71cbd1625b7d29b66f0b5533d158aa65a91',
    'src/nr_ngx_lifecycle.inl': '9842345606bbf046fed86993de9e81832b0238e5',
    'src/nr_optional_layer_probe.h': '7a39868c9c688bf65739695665c236da891f4977',
    'src/nr_post_boundary.h': 'a82e23db292a6bc9cdb9672205eb19ea7c417b4a',
    'src/nr_post_boundary_native.inl': '7e2309ee4565385eacef2ce4bccf121464db3694',
    'src/nr_post_boundary_runtime.inl': '8653482aa9023c26dd967eb98c8cc568073f19ad',
    'src/nr_pre_fallback.h': '4929391e41cc353d110a207ebc5abc932f66864d',
    'src/nr_precision_ui.h': '1178a7c3d81d4d91220230283c5a4fff3ffd65a6',
    'src/nr_probe_policy.h': '79e7971268d8ba0535cb41cb8ae48d0540c9a177',
    'src/nr_recommended_picture.inl': 'faffaf2f6754be2e6cb4657939e86edf4ba2137a',
    'src/nr_region_inputs.h': 'd7ad0d6479d7acf2cd18f4c859093986f5cfacb5',
    'src/nr_region_runtime.inl': 'bb0ccfe1895e0a51dba90d6f8833ec0d5742c5a1',
    'src/nr_region_shader.h': '856bf3db522bb657b53562081fbf5c416bb74b14',
    'src/nr_replay_format.h': '59a54dad6733d20b4bf95421216730216be34558',
    'src/nr_replay_gpu.inl': 'a520b803ec873361834043f6c6d4a48612fd30aa',
    'src/nr_replay_runtime.inl': '6f39bec52e68e7db074d4ce1500388928290f6c0',
    'src/nr_replay_shader.h': '208de4fe323b7b9281d0f1d6acefc985e0955b0a',
    'src/nr_resource_recovery.h': '00ccad3695ed4bb38016938dfd764242a80122a1',
    'src/nr_resource_recovery_runtime.inl': '6cf76f82dedfbafc4723008ff1639ad1c1e62096',
    'src/nr_resource_recovery_semantics.inl': '0a682bd286a2a96fc9ea533e1770896ff2d08021',
    'src/nr_resource_semantics.h': '81874dae21f36985d6b78a9b7e738f2c1d251f99',
    'src/nr_route_compatibility.h': '3e7006e48ea39639bb9a3ea59a6abe5414f9f377',
    'src/nr_route_compatibility_runtime.inl': '7c3b93dc457f896087d179d7d6b9ffe04ff907ad',
    'src/nr_rr_depth_adapter.inl': '5b53f938103829bc90a3f442bc442de184dde528',
    'src/nr_rr_depth_projection.h': 'c103ac108afd942a92916117b1c6a5aaed8e21c7',
    'src/nr_rr_depth_shader.h': 'd0b6a233f60474bbcb81c244aad99fd1440ca63e',
    'src/nr_runtime_load_evidence.inl': '3910e7dc5933346715d8c9676c9934085c751c59',
    'src/nr_runtime_timing.h': '4bad9428aaaf4d2fcae4471b65b7745cdaba50f1',
    'src/nr_runtime_timing.inl': '4837ef46b019a255971687b9a13516ca8b65c04a',
    'src/nr_settings_notices_ui.inl': '01a31b4a22c8017ff21648547cdb0dd7a01bfd9c',
    'src/nr_settings_ui.inl': '61c8577f50d2fc054d249e484747d3c4ceec9604',
    'src/nr_skin_protection.h': 'b09ae7909e8c06452406ecc49e174c6ba8eda4a3',
    'src/nr_stage_capture.inl': '7737bba75d6c0cdf3a5421e02e5e26ade4bab355',
    'src/nr_store_comparison.h': 'e10d1e29846aee709700e25490323aad6b74bfcc',
    'src/nr_submitted_fence_wait.inl': '8f96c99d1af2f4b5e9b034a60950129704ae9080',
    'src/nr_timing_feedback.inl': '0b2ae064077a9aff482e0ce2bcb692caa2f0a12e',
    'src/nr_ui_activity.h': 'e253a492224758ab9757562ff133215a0cf0989f',
    'src/nr_ui_activity_feedback.inl': 'a5b58e91e9ebde28d39c76d706076984ef2b35fa',
    'src/nr_ui_activity_runtime.inl': '02d7f0f976113b48da38f2431dcfb722da243eae',
    'src/nr_work_config.h': '17a66d6d4fbdb644fe7ce720399d6a2907a8f907',
    'scripts/build-nrchain.ps1': '999c2695e86f46e7a78d8434d189e05f244d911d',
    'src/nr_compat_present_events.inl': '2cdf624042d8f28db829dbc646bd83589707312d',
    'src/nr_compat_present_feedback.inl': '7030dce6c69cd09c58d71dcb1f125936b0b85bb3',
    'src/nr_compat_present_models.inl': 'd285eceb2c8ec3571ac7b136eb39eb0c6103e8ef',
    'src/nr_compat_present_policy.hpp': '6a7a92b1499bfed9ed7b0d20ae3b501f63040031',
    'src/nr_compat_present_resources.inl': '7335091cf1643e31587587713de1850b7500865c',
    'src/nr_compat_present_runtime.inl': '98b67d5d0d763cbe27c14059accfdc69c7aae8ec',
    'src/nr_compat_present_shader.h': '758909e400925084e894a2a30483f5b6816a532c',
    'src/nr_fg_capture.inl': 'a766be1840cf7cfbddaad1897aa8270ff2f59a57',
    'src/nr_fg_consumer.inl': '4f9914ffffeea67464cd8b928a680ebea806bae2',
    'src/nr_fg_contract.hpp': '2bfd2d9e42172bb5cf5202001a5625d8804f0bf7',
    'src/nr_fg_hud_runtime.inl': '56ecc6a205bc91e5df36b2e738ef775935367607',
    'src/nr_fg_hud_shader.h': '8198d78dbd6c4cafa9cc53aac284b0e1996e26ff',
    'src/nr_fg_streamline.inl': '45816182861d5cd4462b23b2c8257897e3b022ff',
    'src/nr_full_capture.inl': 'd745291bff09f3dd0110e3cab54c7cf2179f3318',
    'src/nr_full_feedback.inl': 'd50cb946a154812f7d835120bbcf785b37ecf993',
    'src/nr_full_pass.hpp': '27632bb9502d8ac3cd63f84993e3a25b3e6a98c7',
    'src/nr_full_runtime.inl': '278c0fba0b6eb30e8b52399c7c787ef41c5dcbf9',
    'src/nr_present_capture.inl': 'caa777f80fc0d8d44dbcf7b906680bd50c7a2672',
    'src/nr_processing_route.h': 'edea93e4ce9e58972914417cd90971040aeca7db',
    'src/nr_processing_runtime.inl': '6d21d6665c59126749ccc7df6c21df595c5c7ed5',
    'src/nr_energy_gate.h': '14e44bcee19251e3750dc67545b157ccec0944b2',
    'src/nr_late_present_receipt.hpp': '6ee58950ffa43da4d65a9706f10a57835d3db4fd',
    'src/nr_source_frame.hpp': '84414e6c3ae65a31c1e8fff49a8493fe21eeed4f',
    'src/nr_source_frame_forward.inl': '4c3bb4c112f75966a75aeb02c74713c1fef28249',
    'src/nr_source_state_table.inl': 'ff32a36ba0e57e59df995a9880f95aa3c9e1acc4',
    'src/nr_source_frame_runtime.inl': '0ff6df3166ff3a8287af9aef33c9abfdf0759f11',
    'src/nr_source_observer.inl': 'dbc04d1fa6ee1492d614597c1f35cc7ed37e77ac',
    'src/nr_source_transport.inl': '4952bf25c7e378b3d069557de46f30d5d8ca297a',
    'src/nr_source_views.inl': '2d909758aa22be7f2dd0baaf29025df9e2de41d7',
    'src/nrchain_nvngx.cpp': '6bd0f12f291290915b5499c6988f75ee5b53c5a0',
    'src/nr_source_present_feedback.inl': '6cbed3841819df5846944e364fd413be28b398b9',
    'src/nr_native_guide_snapshot.inl': 'aae51b2960a55279bbee05752856992b4b012ffd',
    'src/nr_source_guide_diagnostics.inl': 'e11157f5f2e7a56e24aa248b0ca83b1f28a142b7',
    'src/nr_fg_presentation.h': '177e84c8700fdd7d394769fd492c6d998208a1bb',
    'src/nr_descriptor_scratch.h': '95e1f7f2bee0d89f6c313852cfc6053a2ab97096',
    'src/nr_launch_preferences.h': '1ab61e676d1b2d1ad35dc324160ad5c1db781a30',
    'src/nr_layer_config.h': 'e4a5e8e0d09d454eb7156e032c373a39acc9fa38',
    'src/nr_layers_ui.inl': 'fe4692a2e7c6dfa28fc3bfe4e26da95c66327079',
    'src/nr_unified_ui_policy.h': 'da3c00cc3541fae012ccb2a8175ccf77b47f298e',
    'src/nr_rr_projection_source.h': '8d819fa82f2f9921cd46baf74dffdf7700f29d9a',
    'src/nr_process_memory.h': 'b680d4af415b2f70204b662d75f5ad2c89800c37',
}
BRIDGE_BLOBS = {
    'src/bridge.inc': '244e3ae373b33b40763dc36e8ce967e152cbb505',
    'src/dlss5-bridge.cpp': 'f1337f31cfe3b42a950cb3df2fc83d2190ec5506',
    'src/vkmirror.inc': '8f21f435262b627b8445c215bc975e96205d6312',
}

def git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f'Cannot load source adapter {path.name}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def is_ancestor(root: Path, commit: str) -> bool:
    return subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor',
        commit, 'HEAD'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0

def configure(core: Path):
    for ancestor in REQUIRED_ANCESTORS:
        if not is_ancestor(core, ancestor):
            raise ValueError(f'Missing integration ancestor {ancestor}; merge, do not copy its files')
    # Resolve all reviewed overrides before exposing a configured transformer.
    # The library retains its original pins as a historical standalone fixture.
    for path, expected in INTEGRATION_CORE_PINS.items():
        actual = git(core, 'rev-parse', 'HEAD:' + path).decode().strip()
        if actual != expected:
            raise ValueError(f'Integrated source changed: {path}; review and repin, do not restore old UI')
    m = load('bridge_045_prerequisite', core / 'scripts/prepare-native-bridge-045.py')
    fmt = load('format_patch', core / 'scripts/patch-format-access.py')
    post = load('post_patch', core / 'scripts/patch-post-compat.py')
    m.EXPERIMENT = m.legacy.EXPERIMENT = VERSION
    # Historical adapter validates the inherited source tag before replacing it
    # with EXPERIMENT. The candidate tag is not the raw Core input identity.
    m.VERSION = VERSION
    m.legacy.UPSTREAM = UPSTREAM
    m.legacy.UPSTREAM_BLOBS = dict(BRIDGE_BLOBS)
    m.legacy.CORE_BLOBS.update(INTEGRATION_CORE_PINS)
    prior_patch = m.patch_core
    prior_ui = m.legacy.patch_ui
    def patch_core(text: str) -> str:
        return post.patch_core(fmt.patch_core(prior_patch(text)))
    m.legacy.patch_core = patch_core
    m.legacy.patch_ui = lambda text: post.patch_ui(prior_ui(text))
    previous_prepare = m.prepare
    def prepare_with_native_abi(core_root: Path, upstream: Path, stage: Path) -> None:
        previous_prepare(core_root, upstream, stage)
        # The historical archive exports only build.ps1 from scripts. Copy the
        # explicitly pinned shader compiler inputs into this candidate as well.
        for relative in ('src/nr_present_mapping.h', 'src/nr_launch_preferences.h', 'src/nr_descriptor_scratch.h', 'src/nr_fg_presentation.h', 'scripts/compile-codec-artifact.cpp', 'scripts/prepare-codec-artifacts.py', 'scripts/compile-aux-artifact.cpp', 'scripts/prepare-aux-artifacts.py', 'scripts/build-nrchain.ps1'):
            destination = stage / 'core' / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(git(core_root, 'show', 'HEAD:' + relative))
        abi = load('native_parameter_abi', core_root / 'scripts/patch-ngx-parameter-abi.py')
        abi.apply(stage)
        exposure = load('exposure_adapter', core_root / 'scripts/patch-exposure-access.py')
        exposure.apply(stage)
        completion = load('carrier_completion', core_root / 'scripts/patch-carrier-completion.py')
        completion.apply(stage)
    m.prepare = prepare_with_native_abi
    return m

def file_hashes(root: Path) -> dict[str, str]:
    result = {}
    for p in sorted(root.rglob('*')):
        if p.is_symlink():
            raise ValueError('Generated sources must not contain symlinks')
        if p.is_file() and p != root / 'source-manifest.json':
            result[p.relative_to(root).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    return result

def finalize(core: Path, stage: Path, source: str) -> None:
    # The legacy exporter substitutes two Git probes with fixed metadata. A
    # script-local LASTEXITCODE shadows the global variable that native tools
    # update, so a failed compiler could incorrectly proceed to linking.
    build = stage / 'core/scripts/build.ps1'
    build_text = build.read_text(encoding='utf-8')
    build_text = build_text.replace('beta0.5-dline21-223fix2', VERSION)
    if build_text.count('\n$LASTEXITCODE = 0\n') != 2:
        raise ValueError('Generated build source-identity status anchors changed')
    build.write_text(build_text.replace('\n$LASTEXITCODE = 0\n',
        '\n$global:LASTEXITCODE = 0\n'), encoding='utf-8', newline='\n')
    runtime = stage / 'core/src/nr_before_sr.cpp'
    runtime_text = runtime.read_text(encoding='utf-8')
    old_rr_meter = 'if (signature.rr_bypass) return "A1-native-RR-bypass";'
    if runtime_text.count(old_rr_meter) != 1:
        raise ValueError('RR cadence label anchor changed')
    runtime.write_text(runtime_text.replace(old_rr_meter,
        'if (signature.rr_bypass) return "A1-RR-owner-observed";'),
        encoding='utf-8', newline='\n')
    names = stage / 'core/src/interop/nr_deployment_names.h'
    text = names.read_text(encoding='utf-8')
    old = ('return Equal(name, L"dlss5-native-carrier-045-exp3.addon64") ||\n'
           '        Equal(name, L"dlss5-native-carrier-multiapi-dev3.addon64") ||\n'
           '        Equal(name, L"dlss5-native-carrier-045-dx11-compat.addon64");')
    if text.count(old) != 1:
        raise ValueError('Companion identity anchor changed')
    names.write_text(text.replace(old,
        f'return Equal(name, L"{CARRIER_NAME}");'),
        encoding='utf-8', newline='\n')
    deployment = stage / 'core/src/interop/nr_dx11_deployment.inl'
    text = deployment.read_text(encoding='utf-8')
    old = ('return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire) ||\n'
           '        !nr_dx11_deployment::Read().complete;')
    if text.count(old) != 1:
        raise ValueError('Deployment policy anchor changed')
    deployment.write_text(text.replace(old,
        'return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire);'),
        encoding='utf-8', newline='\n')
    post = load('post_finalize', core / 'scripts/patch-post-compat.py')
    feedback = stage / 'core/src/nr_feedback_capture.inl'
    feedback_text = feedback.read_text(encoding='utf-8')
    activity = 'void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {'
    if feedback_text.count(activity) != 1:
        raise ValueError('Post feedback activity anchor changed')
    feedback.write_text(feedback_text.replace(activity,
        activity + '\n#include "interop/nr_post_feedback.inl"', 1), encoding='utf-8', newline='\n')
    descriptors = stage / 'core/src/nr_codec_descriptors.inl'
    descriptors.write_text(post.patch_descriptors(descriptors.read_text(encoding='utf-8')),
                           encoding='utf-8', newline='\n')
    rc = stage / 'core/src/nr_before_sr.rc'
    resource_text = rc.read_text(encoding='utf-8')
    if '0.5.1 dline21-uniform3 beta' not in resource_text:
        raise ValueError('Missing expected inherited PE description')
    rc.write_text(resource_text,
                  encoding='utf-8', newline='\n')
    ini = stage / 'core/config/nr_before_sr.ini'
    with ini.open('a', encoding='utf-8', newline='\n') as stream:
        stream.write('\n; NR work after SR/RR, not game render scale.\nRRPostEnabled=1\nPostWorkPercent=100\n')
    mf = stage / 'source-manifest.json'
    manifest = json.loads(mf.read_text(encoding='utf-8'))
    manifest['integration'] = {
        'version': VERSION, 'display_version': DISPLAY_VERSION, 'source_commit': source,
        'core_pe_version': '0.5.1.26',
        'cumulative_corefix_source': 'd7a6def43a58ea6c4b26c553547429a8ef88b735',
        'cumulative_corefix_dependencies': [218,219,220,221,230,232,238,240,241,242,243,245],
        # Retain the original dual-parent field for existing artifact readers.
        'parents_in_history': [PR131, PR132],
        'required_ancestors': list(REQUIRED_ANCESTORS),
        'picture_pr134_ancestor': PR134,
        'picture_source_pins': INTEGRATION_CORE_PINS,
        'bridge_release': 'v1.4.12', 'bridge_commit': UPSTREAM,
        'ngx_parameter_abi': 'Microsoft x64 native 17-slot layout; MSVC and Windows-GNU declarations',
        'reset_132_source_blob': STATE_132, 'reset_source_blob': STATE_CURRENT,
        'feedback': 'F8 single TXT; F7 optional paired archive',
        'optional_layers': 'sparse incoming NGX resource metadata; untyped pointers never inspected; no masks exported or sampled',
        'model_baseline': 'seven model controls only, with generation-bound undo; no automatic reset or face-fix claim',
        'face_diagnostic': 'nine labeled transient groups; moved ROI plus full-screen PNG; actual tuning/fence and exact original-config restore',
        'extra_strength_policy': 'user range 1..4; one-time saved migration; internal zero-native contract retained',
        'jitter_policy': 'once-to-zero migration then respect explicit user re-enable; native SR jitter unchanged',
        'region_composition_runtime': True,
        'immutable_bindings': 'up to 8 packets per context; exact view/resource equality; existing command-list and fence retirement; no GPU wait',
        'dual_nr_defaults': 'one layer; one chain scale, 1-5 independent models at equal extents; legacy auxiliary ratios archived',
        'dual_nr_feedback': 'configuration-scoped observation window, successful model tuning/reset reasons, exact binding refusal and guide availability',
        'region_selection': 'fixed pre-gain model difference with snapshotted depth/normals; reflection requires specular albedo and hit distance',
        'post_low_grid_composition': True,
        'face_isolation_runtime': True,
        'face_isolation_game_verified': False,
        'face_backend': 'optional pinned YuNet and ONNX Runtime 1.23.2 CPU; opt-in; game/performance unverified',
        'abc_new_changes_merged': True,
        'post_feedback': 'requested state and sampled RR counters; not GPU completion; same TXT',
        'blend_feedback': 'four observed native blend factors from the same locked state sample; explicit unobserved/unavailable',
        'format_path': 'Exact typed/native view -> unique explicit contract and successful device capability -> ambiguous native fallback; physical storage and typed access reported separately',
        'exposure_path': '1x1 mip0 final-scale red; typed R8_UNORM or established R16/R32 FLOAT-family reads with actual Texture2D/ShaderLoad proof; queryAttempted distinguishes missing query from driver failure; no source UAV',
        'resource_recovery': 'same-process normal-carrier retry after semantics resolve; actual consumed immutable or owned descriptor table, NR/native results, matching Execute and sparse actual stored-value read; failed interpretation remains latched across observer cache churn',
        'compatibility_verdict': 'scoped current first block, retained failure and matching attempt/proof; technical proof requires three correlated carriers; final Present visual remains unobserved without separate F7 proof',
        'native_observer': 'layered Create/Evaluate/Release identity; external Feature18 ownership distinct from private NR; gaps remain fail-closed',
        'post_boundary': 'immutable scoped probe window with native barrier/write/submit evidence; no inferred final-screen write or automatic state rewrite',
        'optional_inventory_blocks_on_failure': False,
        'vulkan_native': 'disabled: submit synchronization not implemented',
        'synthetic_legacy': 'not integrated',
        'carrier_filename': CARRIER_NAME,
        'carrier_completion': 'actual GPU progress before DX11 copyback; 500ms frame timeout quarantines resources until restart; no CPU completion signal',
        'carrier_faults': 'NGX hardware exceptions propagate; ordinary error codes retain native fallback',
        'rr_post': 'Feature13 native output; hardware depth or current standard ViewSpace projection converted privately; same NR core; game pending',
        'post_work': '25-100 percent Npost/O; 0 disables while retaining precision; invalid requests rejected',
        'rr_linear_depth': 'positive view-Z standard perspective to private R32 HW depth; matrix-derived inversion; unknown projection stays native with F8',
        'compatibility_source': 'd7a6def43a58ea6c4b26c553547429a8ef88b735',
        'compatibility_previous_source': '6a0ad7684683993328a7a98a9bea83ac76b01a64',
        'light_style': 'whole light/dark and fine-change modes retained; D13 groups common controls and moves explanations to hover',
        'light_control_mode': 'INI missing=0; template/recommended full-picture reset=1; group presets retain mode; composition only',
        'post_light_style_game_verified_at_build': False,
        'jitter': 'resolved post color zero jitter; native guide phase requires runtime validation',
        'fg_control': 'not implemented', 'getdesc_spoof': 'not implemented',
        'mfg_scope': 'Manager FG/MFG is independent under plan 0838ffb; this Core candidate adds no FG backend',
        'game_validation': 'owner will test final batch; old BG3 result retained separately',
    }
    manifest['files'] = file_hashes(stage)
    mf.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    if json.loads(mf.read_text(encoding='utf-8'))['files'] != file_hashes(stage):
        raise ValueError('Generated manifest mismatch')

def prepare(core: Path, upstream: Path, output: Path) -> None:
    if output.exists():
        raise ValueError('Output already exists')
    if git(core, 'status', '--porcelain', '--untracked-files=no').strip():
        raise ValueError('Commit reviewed source changes first')
    if git(upstream, 'rev-parse', 'HEAD').decode().strip() != UPSTREAM:
        raise ValueError('Upstream is not the pinned v1.4.12 commit')
    source = git(core, 'rev-parse', 'HEAD').decode().strip()
    m = configure(core)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nr-integration-', dir=output.parent) as tmp:
        stage = Path(tmp) / 'candidate'
        m.prepare(core, upstream, stage)
        finalize(core, stage, source)
        if git(core, 'rev-parse', 'HEAD').decode().strip() != source or \
                git(core, 'status', '--porcelain', '--untracked-files=no').strip():
            raise ValueError('Source changed during generation')
        if output.exists():
            raise ValueError('Output appeared during generation')
        m.legacy.publish_stage(stage, output)

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--core', type=Path, default=ROOT)
    ap.add_argument('--upstream', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())
    print(f'Staged {VERSION}: source only, not game-verified')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
