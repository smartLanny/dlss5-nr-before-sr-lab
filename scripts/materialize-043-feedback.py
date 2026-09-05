"""One-shot integration against the reviewed PR48 baseline.

Writes source blobs only when --publish-blobs is supplied. Never commits,
changes a ref, merges a PR or modifies a release. The connector imports the
reviewed blobs into the development branch. Remove this bootstrap before merge.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    'src/nr_before_sr.cpp': '909ed10c9406de47b0a94a32c6162750c4ae32f2',
    'src/nr_f8_sequence.inl': 'd8aeb2b205496311535dbc326a18f00f35ef086a',
    'src/nr_diagnostic_bundle.inl': '61c866518763e84d3b2368c52b3bda2a29118f4f',
    'src/nr_feedback_core.inl': '07f890b39e4bfeaf94893353e73e34927cd5b770',
}
def blob(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--publish-blobs', action='store_true')
    args = parser.parse_args()
    changes = {}
    for path, expected in EXPECTED.items():
        data = (ROOT / path).read_bytes()
        if blob(data) != expected:
            raise RuntimeError(f'{path}: source identity changed; review before updating')
        changes[path] = data.decode('utf-8-sig')

    def edit(path, old, new, count=1):
        s = changes.get(path)
        if s is None:
            s = (ROOT / path).read_text(encoding='utf-8-sig')
        if s.count(old) != count:
            raise RuntimeError(f'{path}: expected {count} instances of {old[:100]!r}, found {s.count(old)}')
        changes[path] = s.replace(old, new)

    cpp = 'src/nr_before_sr.cpp'
    edit(cpp, '#include "nr_f8_sequence.inl"', '#include "nr_feedback_core.inl"\n#include "nr_f8_sequence.inl"')
    edit(cpp, 'nr_f8::g_hotkey.store(key>=VK_F1 && key<=VK_F12?key:VK_F8);',
         'nr_f8::g_hotkey.store(nr_feedback_text::CoreHotkey(key));')
    edit(cpp, '        nr_f8::RequestWithOptions({nr_f8::Comparison::Current,false,0,0});',
         '        nr_f8::RequestCore();')
    edit(cpp, '    if(!nr_f8::IsActive())FeedbackPresent(runtime);',
         '    // Core feedback has no GPU-stage request and runs without eligible NR frames.')
    edit(cpp, '    nr_f8::Present(runtime);\n\n    const SHORT f7_state',
         '    const SHORT f7_state')
    edit(cpp, '''        const bool accepted = RequestStageCapture();
        Log("[stage-capture] F7 request=%s; next eligible warmed-up NR "
            "composition. '-' screenshots are separate, NOT same-frame evidence.",
            accepted ? "accepted" : "already pending");''', '''        const bool accepted = nr_f8::RequestWithOptions({nr_f8::Comparison::Current,false,0,0});
        if (accepted) {
            nr_f8::OpenWindow();
            runtime->open_overlay(true, reshade::api::input_source::keyboard);
        }
        Log("[feedback] F7 image supplement request=%s; a fresh paired core package precedes exact-fence images.",
            accepted ? "accepted" : "busy");''')
    edit(cpp, '    stage_key_was_down = stage_key_is_down;', '''    stage_key_was_down = stage_key_is_down;
    // Start after both shortcut handlers; even a single remaining Present can
    // queue the bounded core worker. The image coordinator waits for its pair.
    nr_core_feedback::Present(runtime);
    nr_f8::Present(runtime);''')

    bundle = 'src/nr_diagnostic_bundle.inl'
    edit(bundle, '    unsigned long long archive_bytes = 0, read_bytes = 0, started = 0;',
         '    unsigned long long archive_limit = kArchiveLimit; // May lower, never raise, the global ceiling.\n    unsigned long long archive_bytes = 0, read_bytes = 0, started = 0;')
    edit(bundle, 'bool Write(Job &job, const void *data, DWORD size) {\n    if (job.archive_bytes + size > kArchiveLimit)',
         'unsigned long long ArchiveLimit(const Job &job) { return std::min(job.archive_limit, kArchiveLimit); }\n\nbool Write(Job &job, const void *data, DWORD size) {\n    if (job.archive_bytes > ArchiveLimit(job) || size > ArchiveLimit(job) - job.archive_bytes)')
    edit(bundle, '''    if (item.kind!=FileKind::HashOnly && bytes+sizeof(job.manifest)+65536>
        kArchiveLimit-job.archive_bytes)''', '''    if (item.kind!=FileKind::HashOnly && (job.archive_bytes > ArchiveLimit(job) ||
        bytes+sizeof(job.manifest)+65536 > ArchiveLimit(job)-job.archive_bytes))''')

    core = 'src/nr_feedback_core.inl'
    edit(core, '''    const auto id = nr_feedback_text::SessionId(GetCurrentProcessId(), GetTickCount64(), ++g_serial);
    nr_bundle::CopyText(g_pair.session, sizeof(g_pair.session), id.c_str());''', '''    // No allocation or exception is possible while holding the publication lock.
    std::snprintf(g_pair.session, sizeof(g_pair.session), "DLSS5_CORE_%lu_%llu_%llu",
        GetCurrentProcessId(), GetTickCount64(), ++g_serial);''')
    edit(core, '    const auto *device = runtime->get_device();', '    auto *device = runtime->get_device();')
    edit(core, '    auto &s = session->activity;\n    s.started = GetTickCount64();', '    try {\n    auto &s = session->activity;\n    s.started = GetTickCount64();')
    edit(core, '''        delete session; Publish({}, false, "core worker queue failed; no previous ZIP reused");
    }
}
} // namespace nr_core_feedback''', '''        delete session; Publish({}, false, "core worker queue failed; no previous ZIP reused");
    }
    } catch (...) {
        AcquireSRWLockExclusive(&g_feedback_lock);
        if (g_feedback_session == &session->activity) g_feedback_session = nullptr;
        ReleaseSRWLockExclusive(&g_feedback_lock);
        delete session; Publish({}, false, "core snapshot allocation failed; no game state changed");
    }
}
} // namespace nr_core_feedback''')

    seq = 'src/nr_f8_sequence.inl'
    edit(seq, '    FeedbackSession activity = {};', '    FeedbackSession activity = {};\n    nr_core_feedback::PairSnapshot core_pair = {};')
    edit(seq, 'void Cancel() { g_cancel.store(true); }', '''void Cancel() {
    g_cancel.store(true);
    if (g_status.load()==Status::Requested) nr_core_feedback::Cancel();
}
bool RequestCore() {
    AcquireSRWLockExclusive(&g_control);
    const bool accepted = !IsActive() && g_status.load()!=Status::Packaging &&
        StageCaptureStatus()!=1 && StageCaptureStatus()!=2 && nr_core_feedback::Request();
    ReleaseSRWLockExclusive(&g_control);
    return accepted;
}''')
    edit(seq, 'bool accepted = !IsActive() && g_status.load()!=Status::Packaging && StageCaptureStatus()!=1 && StageCaptureStatus()!=2;',
         'bool accepted = !IsActive() && g_status.load()!=Status::Packaging && StageCaptureStatus()!=1 && StageCaptureStatus()!=2 && nr_core_feedback::Request();')
    edit(seq, '    auto *s=static_cast<Session *>(opaque);', '''    auto *s=static_cast<Session *>(opaque);
    FeedbackAppend(s->activity, "capture.kind=image-supplement\\ncore.session=%s\\ncore.config=%u\\ncore.complete=%d\\n",
        s->core_pair.session, s->core_pair.config, s->core_pair.complete ? 1 : 0);''')
    edit(seq, '        if(g_cancel.load()){g_status.store(Status::Idle);ReleaseSRWLockExclusive(&g_control);return;}',
         '''        if(g_cancel.load()){nr_core_feedback::Cancel();g_status.store(Status::Failed);ReleaseSRWLockExclusive(&g_control);return;}
        if(nr_core_feedback::Busy()){ReleaseSRWLockExclusive(&g_control);return;}''')
    edit(seq, '        s->id=++g_next_session;s->options=g_options;s->runtime=runtime;s->started=GetTickCount64();',
         '''        s->id=++g_next_session;s->options=g_options;s->runtime=runtime;s->started=GetTickCount64();
        s->core_pair=nr_core_feedback::SnapshotPair();
        s->partial=!s->core_pair.ready || !s->core_pair.complete;''')
    edit(seq, 'void DrawControls() {', '''void DrawCoreStatus() {
    const bool zh=UseChineseUi();
    const auto status=g_feedback_status.load();
    if(status==0)return;
    char path[4096]={},reason[160]={};
    AcquireSRWLockShared(&g_feedback_lock);
    WideToUtf8(g_feedback_last_zip,path,sizeof(path));
    nr_bundle::CopyText(reason,sizeof(reason),nr_core_feedback::g_reason);
    ReleaseSRWLockShared(&g_feedback_lock);
    if(status==1 || status==2) {
        ImGui::TextWrapped("%s",zh?"核心反馈：正在记录状态和生成日志小包，不等待 NR 图像。":"Core feedback: recording state and compact logs; no NR images required.");
        if(ImGui::Button(zh?"取消核心采集":"Cancel core capture"))nr_core_feedback::Cancel();
    } else {
        ImGui::TextWrapped("%s",status==3?(zh?"核心反馈包已生成。":"Core feedback package saved."):
            status==4?(zh?"核心反馈包部分完成，请保留其中的缺失原因。":"Core feedback partially complete; missing reasons are included."):
            (zh?"核心反馈保存失败，没有复用旧包路径。":"Core feedback failed; no old package path reused."));
        if(reason[0])ImGui::TextWrapped("%s",reason);
        if(path[0]){ImGui::TextWrapped("%s",path);if(ImGui::Button(zh?"复制核心包路径":"Copy core path"))ImGui::SetClipboardText(path);}
    }
}
void DrawControls() {''')
    edit(seq, '    const auto state=g_status.load();\n    const bool use_chinese_ui=UseChineseUi();',
         '''    const auto state=g_status.load();
    const bool use_chinese_ui=UseChineseUi();
    DrawCoreStatus();
    const bool busy=IsActive() || state==Status::Packaging || nr_core_feedback::Busy();
    ImGui::BeginDisabled(busy);
    if(ImGui::Button(use_chinese_ui?"采集核心反馈包（F8，推荐）":"Collect core feedback (F8, recommended)"))RequestCore();
    F8Tooltip(use_chinese_ui?"用于不生效、旁路和兼容性。只保存有界日志与状态，不采图片、不改设置，ZIP 上限 8 MiB。":"For bypass and compatibility issues. Bounded logs and state only; no images or setting changes. ZIP ceiling: 8 MiB.");
    ImGui::EndDisabled();''')
    edit(seq, 'if(!IsActive() && state!=Status::Packaging) {', 'if(!busy) {')
    edit(seq, '"采集当前问题（F8）":"Capture current issue (F8)"', '"采集图像补充包（F7，仅画面异常）":"Capture image supplement (F7, visual issues only)"')
    edit(seq, '"只记录当前状态，不启用 NR、不切换模式、不修改设置。":"Records the current state without enabling NR, switching modes or changing settings."',
         '"先生成新的配对核心包，再采当前图像；用于颜色、亮度、闪烁和细节问题。请一起提交界面显示的新核心包。默认不改设置，图像部分可能等待 GPU 或标记缺失。":"Creates a fresh paired core package before images. Use for color, lighting, flicker and detail issues; submit the new core path too. Default capture changes no settings; missing GPU images are explicit."')
    edit(seq, 'g_hotkey.store(static_cast<unsigned>(key));', 'g_hotkey.store(nr_feedback_text::CoreHotkey(static_cast<unsigned>(key)));')
    edit(seq, '            ImGui::Text("F%d",key-VK_F1+1);', '''            key=static_cast<int>(g_hotkey.load());
            ImGui::Text("F%d",key-VK_F1+1);
            ImGui::TextDisabled("%s",use_chinese_ui?"F6 总开关、F7 图像采集已保留；冲突设置回到 F8。":"F6 master and F7 images are reserved; conflicting settings fall back to F8.");''')
    edit(seq, '"完整反馈包已生成。":"Complete package saved."', '"图像补充包已生成，请与新核心包一起提交。":"Image supplement saved; submit its paired core package too."')
    edit(seq, '"部分反馈包已生成，缺失原因已记录，仍可上传分析。":"Partial package saved with missing-data reasons."',
         '"图像补充包部分完成，缺失原因已记录。":"Image supplement partially complete; missing-data reasons included."')
    edit(seq, '"保存失败，请检查日志及磁盘空间。":"Package failed; check log and disk space."',
         '"图像采集失败或已取消，请检查核心包及磁盘空间。":"Image capture failed or was cancelled; inspect the core package and disk space."')
    edit(seq, '"复制路径":"Copy path"', '"复制图像包路径":"Copy image path"')
    edit(seq, 'if(!g_window_open.load() && !IsActive() && g_status.load()!=Status::Packaging)return;',
         'if(!g_window_open.load() && !IsActive() && g_status.load()!=Status::Packaging && !nr_core_feedback::Busy())return;')
    edit(seq, '"DLSS5 反馈采集":"DLSS5 feedback capture"',
         '"DLSS5 反馈采集###dlss5-feedback-window":"DLSS5 feedback capture###dlss5-feedback-window"')

    # Existing coordinator tests keep the actual sequence. Only the newly
    # external core service is a seam; actual core IO is tested separately.
    test = 'tests/f8_controller_harness.cpp.in'
    edit(test, 'PRODUCTION_SEQUENCE', '''#include "nr_feedback_text.h"
namespace nr_core_feedback {
struct PairSnapshot { char session[96]="DLSS5_CORE_fixture"; unsigned config=1; bool ready=true,complete=true; };
bool Busy(){return false;}
bool Request(){return true;}
void Cancel(){}
PairSnapshot SnapshotPair(){return {};}
}
PRODUCTION_SEQUENCE''')

    ui = 'src/nr_settings_ui.inl'
    edit(ui, '        changed |= ImGui::Checkbox(ui("UI correction", "UI 校正"), &next.ui_correction);',
         '''        changed |= ImGui::Checkbox(ui("UI correction", "UI 校正"), &next.ui_correction);
        ImGui::TextDisabled("%s", ui("New installations: UI protection on; your saved choice is retained.", "新装默认开启：优先保护 UI；保留你已保存的选择。"));''')
    cfg = 'config/nr_before_sr.ini'
    edit(cfg, 'JitterMode=1', '''JitterMode=1
; F8: compact core logs (8 MiB ceiling), no GPU capture or settings changes.
; F7: visual supplement, automatically generates a new paired core first.
; F6/F7 cannot be used as custom core-feedback shortcuts.
FeedbackVirtualKey=119''')
    ci = '.github/workflows/beta042-validate.yml'
    edit(ci, '  pull_request:\n    branches:\n      - codex/beta0.4.2',
         '  pull_request:\n    branches:\n      - codex/beta0.4.2\n      - codex/beta0.4.2-bug-sweep')
    edit(ci, '        run: python3 scripts/validate-bug-sweep.py --compiler g++', '''        run: |
          python3 scripts/validate-bug-sweep.py --compiler g++
          g++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/feedback_text_test.cpp -o /tmp/feedback-text
          /tmp/feedback-text
          python3 -m unittest discover -s tests -p 'test_core_feedback.py'
          python3 tests/test_f8_parser.py''')
    edit(ci, "          if ($LASTEXITCODE -ne 0) { throw 'Bug sweep regression harness failed.' }", '''          if ($LASTEXITCODE -ne 0) { throw 'Bug sweep regression harness failed.' }
          python scripts/validate-core-feedback.py --compiler $compiler
          if ($LASTEXITCODE -ne 0) { throw 'Core feedback production harness failed.' }''')

    for path, text in changes.items():
        (ROOT / path).write_text(text, encoding='utf-8', newline='\n')
    subprocess.run(['git', 'diff', '--check'], cwd=ROOT, check=True)
    if args.publish_blobs:
        if os.environ.get('GITHUB_REPOSITORY') != 'smartLanny/dlss5-nr-before-sr-lab' or os.environ.get('GITHUB_REF') != 'refs/heads/codex/043-feedback-and-triage':
            raise RuntimeError('not the authorized development branch')
        for path, text in changes.items():
            request = urllib.request.Request(
                'https://api.github.com/repos/smartLanny/dlss5-nr-before-sr-lab/git/blobs',
                data=json.dumps({'content':text, 'encoding':'utf-8'}).encode(),
                headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'], 'Accept':'application/vnd.github+json'}, method='POST')
            with urllib.request.urlopen(request, timeout=30) as response:
                sha = json.load(response)['sha']
            expected = blob(text.encode())
            if sha != expected: raise RuntimeError('published blob hash differs')
            print('REVIEW_BLOB '+json.dumps({'path':path, 'mode':'100644', 'type':'blob', 'sha':sha}), flush=True)

if __name__ == '__main__': main()
