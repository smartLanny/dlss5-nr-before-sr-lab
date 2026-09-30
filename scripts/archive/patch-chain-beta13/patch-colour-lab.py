#!/usr/bin/env python3
"""M1 overlay for the reviewed uniform3 generated source; no checkout edits."""
from __future__ import annotations
from pathlib import Path

VERSION = 'beta0.5-dline21-colour3'

POST_COLOUR_LOG = '''            const bool color_follow_active =
                IsUnityExposureContract(color_contract);
            Log("Post-SR FP16 color-follow contract: requested=%.3f "
                "effective=%.3f preExposure=%.6f exposureScale=%.6f "
                "exposureTexture=%d. %s",
                static_cast<double>(g_config.color_strength),
                static_cast<double>(color_follow_active
                    ? g_config.color_strength : 0.0f),
                static_cast<double>(color_contract.pre_exposure),
                static_cast<double>(color_contract.exposure_scale),
                color_contract.exposure_texture != nullptr ? 1 : 0,
                color_follow_active
                    ? "Bounded RGB ratios will follow NR chroma while preserving NR luminance."
                    : "The current non-unity exposure contract remains luminance-only.");'''

LEGACY_HDR_UI = '''        changed |= ImGui::Checkbox(ui("Allow experimental HDR colour", "允许实验 HDR 调色"), &next.allow_unverified_hdr_color);
        SettingsTooltip(ui("Allows colour edits for an unverified HDR input. Turn off if colour shifts or flicker appear; the AI colour slider never changes this permission.", "当前 HDR 颜色无法确认时，允许尝试 AI 调色。出现偏色或闪烁时关闭；调整 AI 色彩强度不会自动开启此项。"));'''

def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f'{label}: expected exactly one anchor, got {count}; review before rebasing')
    return text.replace(old, new, 1)

def patch_runtime(text: str) -> str:
    text = once(text, '#include "nr_layer_config.h"',
        '#include "nr_layer_config.h"\n#include "nr_colour_lab_policy.h"', 'policy include')
    text = once(text, 'struct ChainConfig {',
        'struct ChainConfig {\n    int colour_lab_mode = nr_colour_lab::kDefaultMode; // Composition only.', 'config field')
    load = ('    next.allow_unverified_hdr_color = GetPrivateProfileIntW(\n'
            '        L"NRBeforeSR", L"AllowUnverifiedHdrColor", 0, g_ini_path) != 0;')
    text = once(text, load, load + '\n    next.colour_lab_mode = static_cast<int>(nr_colour_lab::Normalize(\n'
        '        GetPrivateProfileIntW(L"NRBeforeSR", L"ColourLabMode",\n'
        '            nr_colour_lab::kDefaultMode, g_ini_path)));', 'load mode')
    save = ('    record_write(WriteIniInt(L"AllowUnverifiedHdrColor",\n'
            '                             config.allow_unverified_hdr_color ? 1 : 0));')
    text = once(text, save, save + '\n    record_write(WriteIniInt(L"ColourLabMode",\n'
        '        static_cast<int>(nr_colour_lab::Normalize(config.colour_lab_mode))));', 'save mode')
    same = 'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return '
    text = once(text, same, same + 'a.colour_lab_mode == b.colour_lab_mode && ',
        'configuration equality without history reset')
    for variable in ('kAddonVersion', 'kBuildIdentity'):
        old = f'constexpr const char *{variable} = "beta0.5-dline21-uniform3";'
        text = once(text, old, f'constexpr const char *{variable} = "{VERSION}";', variable)
    text = once(text, POST_COLOUR_LOG, '''            const bool verified_exposure = IsUnityExposureContract(color_contract);
            const bool colour_allowed = nr_colour_lab::AllowHdrColour(
                g_config.colour_lab_mode, g_config.allow_unverified_hdr_color, verified_exposure);
            Log("Post-SR FP16 color-follow admission: requested=%.3f "
                "colourAllowed=%d mode=%s legacyRequest=%d verifiedExposure=%d "
                "preExposure=%.6f exposureScale=%.6f exposureTexture=%d. "
                "Final consumer strength is reported by ColorDiag.",
                static_cast<double>(g_config.color_strength), colour_allowed ? 1 : 0,
                nr_colour_lab::Name(g_config.colour_lab_mode),
                g_config.allow_unverified_hdr_color ? 1 : 0, verified_exposure ? 1 : 0,
                static_cast<double>(color_contract.pre_exposure),
                static_cast<double>(color_contract.exposure_scale),
                color_contract.exposure_texture != nullptr ? 1 : 0);''', 'post HDR admission log')
    return text

def patch_policy(text: str) -> str:
    text = once(text, '    ExperimentalUnverifiedHdr,',
        '    ExperimentalUnverifiedHdr,\n    ColourLabPriority, // Append: stable existing diagnostic IDs.', 'reason enum')
    text = once(text, '    if (allow && policy.color_reason == NrColorReason::UnverifiedHdr) {',
        '    // Called under the existing chain/configuration lock for pre AND post composition.\n'
        '    const int lab_mode = g_config.colour_lab_mode;\n'
        '    if (nr_colour_lab::AllowUnverified(lab_mode, allow) &&\n'
        '        policy.color_reason == NrColorReason::UnverifiedHdr) {', 'admission')
    text = once(text, '        policy.color_reason = NrColorReason::ExperimentalUnverifiedHdr;',
        '        policy.color_reason = nr_colour_lab::Normalize(lab_mode) == nr_colour_lab::Mode::ColourPriority\n'
        '            ? NrColorReason::ColourLabPriority : NrColorReason::ExperimentalUnverifiedHdr;', 'reason assignment')
    text = once(text, '    case NrColorReason::UnverifiedHdr: return "unverified-hdr-luminance-only";',
        '    case NrColorReason::ColourLabPriority: return "colour-lab-priority-unverified-hdr";\n'
        '    case NrColorReason::UnverifiedHdr: return "unverified-hdr-luminance-only";', 'reason text')
    return text

def patch_ui(text: str) -> str:
    text = '#include "nr_colour_lab_ui.inl"\n' + text
    return once(text, '    (void)before_sr;',
        '    (void)before_sr;\n    DrawColourLabControls(next, generation, has_pending, changed, apply_discrete_edit);', 'UI transaction')

def patch_settings_ui(text: str) -> str:
    return once(text, LEGACY_HDR_UI, '''        {
            const bool editable = nr_colour_lab::LegacyPermissionEditable(next.colour_lab_mode);
            bool allowed = nr_colour_lab::AllowUnverified(next.colour_lab_mode,
                                                         next.allow_unverified_hdr_color);
            ImGui::BeginDisabled(!editable);
            if (ImGui::Checkbox(ui("Allow experimental HDR colour", "允许实验 HDR 调色"), &allowed) && editable) {
                next.allow_unverified_hdr_color = allowed;
                changed = true;
            }
            ImGui::EndDisabled();
            if (editable) {
                SettingsTooltip(ui("Legacy policy uses this saved HDR permission. The AI colour slider changes strength, not permission.",
                                   "旧版策略使用此已保存的 HDR 许可。AI 色彩滑块只改变强度，不改变许可。"));
            } else {
                SettingsNote(ui("The colour policy controls unverified HDR permission here. The saved Legacy value is retained; select Legacy to edit it.",
                                "此处未验证 HDR 的许可由颜色策略决定。旧版保存值保留；切换到旧版策略后可编辑。"));
            }
        }''', 'legacy HDR permission UI')

def apply(stage: Path, inputs: dict[str, bytes]) -> None:
    transforms = {'src/nr_before_sr.cpp': patch_runtime,
                  'src/nr_color_policy.inl': patch_policy,
                  'src/nr_settings_ui.inl': patch_settings_ui,
                  'src/nr_light_style_ui.inl': patch_ui}
    # Validate every transform before changing any file. The enclosing exporter
    # only publishes a complete temporary stage after manifest verification.
    updates = {path: transform((stage / 'core' / path).read_text(encoding='utf-8'))
               for path, transform in transforms.items()}
    for path, text in updates.items():
        (stage / 'core' / path).write_text(text, encoding='utf-8', newline='\n')
    for path in ('src/nr_colour_lab_policy.h', 'src/nr_colour_lab_ui.inl'):
        (stage / 'core' / path).write_bytes(inputs[path])
