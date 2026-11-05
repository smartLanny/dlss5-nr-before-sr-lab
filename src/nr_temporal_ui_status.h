#pragma once
// Presentation only: never changes route admission, history, settings or GPU work.
namespace nr_temporal_ui {
struct Message { const char* en; const char* zh; bool request_feedback; };
inline Message Describe(unsigned state, bool recent_submission) {
    switch (state) {
    case 0: case 1: return {"Waiting for a supported game frame.", "等待支持的游戏画面。", false};
    case 2: return {"Not applied: the processing queue is unverified.", "未生效：处理队列未确认。", true};
    case 3: return {"Waiting for memory or safe resource release.", "等待显存余量或旧资源安全释放。", true};
    case 4: return {"Not applied: the first-layer reference is unavailable.", "未生效：第一层参考画面不可用。", true};
    case 5: case 16: return {"Not applied: native motion direction, scale or sampling is unverified.", "未生效：原生运动的方向、比例或采样未确认。", true};
    case 6: return {"Waiting for a safe recording slot.", "等待可安全录制的资源槽。", true};
    case 7: return {"Recorded; actual submission is not confirmed yet.", "已录制；尚未确认实际提交。", false};
    case 8: return recent_submission
        ? Message{"Processing submitted; pixel-history adoption is not proven.", "已提交处理；不代表像素历史已采用。", false}
        : Message{"No recent submission; the previous record is historical.", "暂无近期提交；此前记录仅供历史参考。", true};
    case 9: return {"Recording fault. Save F8 feedback and restart with stabilization off.", "处理异常。请保存 F8 反馈，关闭防闪后重启。", true};
    case 10: return {"Not applied: picture colour or exposure is unverified.", "未生效：画面颜色或曝光未确认。", true};
    case 11: return {"Not applied: Later layers requires at least two NR layers.", "未生效：“后续层”需要至少两层 NR。", true};
    case 12: return {"Not applied: candidate picture extent or format is unverified.", "未生效：候选画面的尺寸或格式未确认。", true};
    case 13: return {"Waiting for a new source frame; repeated frames add no history work.", "等待新的源画面；重复帧不会追加历史处理。", false};
    case 14: return {"Recording cancelled before submission; no completed work is claimed.", "录制已在提交前取消；不计为已完成处理。", true};
    case 15: return {"Not applied: native motion data is missing.", "未生效：缺少原生运动数据。", true};
    case 17: return {"Not applied: the motion/depth snapshot is unavailable.", "未生效：运动或深度快照不可用。", true};
    case 18: return {"Not applied: this route requires Whole chain, not Later layers.", "未生效：此路线需要选择“整链”，不支持“后续层”。", true};
    case 19: return {"Not applied: native motion is unsupported on this route. AMDOF is a separate candidate.", "未生效：此路线未支持原生运动；可另行尝试 AMDOF。", true};
    case 20: return {"Not applied: picture snapshot allocation was refused.", "未生效：画面快照分配被拒绝。", true};
    case 21: return {"Not applied: turn off private SR first; saved choices are retained.", "未生效：请先关闭私有 SR；原设置会保留。", true};
    case 22: return {"Not applied: the Present picture source or FG ownership is unverified.", "未生效：Present 画面来源或 FG 归属未确认。", true};
    default: return {"Status unavailable; do not assume stabilization is active.", "状态不可用；不能认定防闪已生效。", true};
    }
}
inline bool Recent(unsigned long long now, unsigned long long tick) {
    return tick != 0 && now >= tick && now-tick < 2000;
}
}
