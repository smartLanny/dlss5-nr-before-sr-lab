#include <cstdlib>
#include <iostream>
#include "src/interop/nr_external_provider.h"
#include "src/interop/nr_external_provider_feeder_map.h"

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { \
    std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static NrExternalFrameV1 MakeFrame() {
    NrExternalFrameV1 f{};
    f.provider_id = kNrExternalProviderIdDl5f;
    f.view_id = 7;
    f.epoch = 2;
    f.frame_id = 1;
    f.lease_flags = static_cast<std::uint32_t>(NrExternalLeaseFlags::OutputSameAsColor);
    f.command_list = 0x1000;
    f.color.resource = 0x2000;
    f.color.width = 64; f.color.height = 32;
    f.color.rect_w = 64; f.color.rect_h = 32;
    f.color.format = 10;
    f.color.provenance = NrGuideProvenance::Synthetic;
    f.output = f.color;
    f.depth.resource = 0x3000;
    f.depth.width = 64; f.depth.height = 32;
    f.depth.rect_w = 64; f.depth.rect_h = 32;
    f.depth.provenance = NrGuideProvenance::Synthetic;
    f.motion.resource = 0x4000;
    f.motion.width = 64; f.motion.height = 32;
    f.motion.rect_w = 64; f.motion.rect_h = 32;
    f.motion.provenance = NrGuideProvenance::Synthetic;
    f.motion.motion_unit = NrExternalMotionUnit::Pixels;
    return f;
}

int main() {
    CHECK(sizeof(NrExternalResourceV1) == 72);
    CHECK(sizeof(NrExternalFrameV1) == 448);
    CHECK(sizeof(NrExternalStatusV1) == 56);
    CHECK(sizeof(NrExternalSubmitResultV1) == 96);
    CHECK(NrGuideProvenanceName(NrGuideProvenance::Native)[0] == 'N');
    CHECK(NrGuideProvenanceName(NrGuideProvenance::Extracted)[0] == 'E');
    CHECK(NrGuideProvenanceName(NrGuideProvenance::Synthetic)[0] == 'S');

    nr_external_provider::Session session;
    nr_external_provider::ConfigSnapshot cfg;
    cfg.master_enabled = true;
    cfg.transfer_strength = 1.0f;
    cfg.observer_epoch = 2;

    auto frame = MakeFrame();
    auto rejected = nr_external_provider::Admit(session, cfg, frame);
    CHECK(!rejected.admit);
    CHECK(rejected.reason == NrExternalProviderReason::Starting);

    CHECK(nr_external_provider::ClaimOwnership(session, cfg, kNrExternalProviderIdDl5f));
    NrExternalStatusV1 status{};
    nr_external_provider::FillStatus(session, cfg, status);
    CHECK(NrExternalStatusAllowsSubmit(status));

    auto first = nr_external_provider::Admit(session, cfg, frame);
    CHECK(first.admit && first.run_nr && first.history_reset);
    CHECK(first.reason == NrExternalProviderReason::Ready);

    frame.frame_id = 2;
    auto second = nr_external_provider::Admit(session, cfg, frame);
    CHECK(second.admit && second.run_nr && !second.history_reset);

    // Same frame id must fail closed.
    auto dup = nr_external_provider::Admit(session, cfg, frame);
    CHECK(!dup.admit);
    CHECK(dup.reason == NrExternalProviderReason::FrameMismatch);

    // Source/provenance switch rebuilds history; Synthetic is not a reject.
    frame.frame_id = 3;
    frame.motion.provenance = NrGuideProvenance::Extracted;
    auto switched = nr_external_provider::Admit(session, cfg, frame);
    CHECK(switched.admit && switched.history_reset);

    // F6/master off: admit identity, no NR work.
    cfg.master_enabled = false;
    frame.frame_id = 4;
    auto off = nr_external_provider::Admit(session, cfg, frame);
    CHECK(off.admit && !off.run_nr);
    CHECK(off.reason == NrExternalProviderReason::Disabled);

    cfg.master_enabled = true;
    frame.frame_id = 5;
    const auto resumed = nr_external_provider::Admit(session, cfg, frame);
    CHECK(resumed.admit && resumed.history_reset);
    const auto last_frame = session.last_frame_id;
    CHECK(nr_external_provider::ClaimOwnership(session, cfg, kNrExternalProviderIdDl5f));
    CHECK(session.last_frame_id == last_frame);
    cfg.competing_nr = true;
    frame.frame_id = 6;
    CHECK(!nr_external_provider::Admit(session, cfg, frame).admit);
    cfg.competing_nr = false;

    // Missing color fails; low-quality Synthetic alone does not.
    cfg.master_enabled = true;
    frame.frame_id = 7;
    frame.color.resource = 0;
    auto missing = nr_external_provider::Admit(session, cfg, frame);
    CHECK(!missing.admit);
    CHECK(missing.reason == NrExternalProviderReason::MissingRequired);

    frame = MakeFrame();
    frame.frame_id = 10;
    frame.lease_flags = 0; // color==output without OutputSameAsColor
    CHECK(!nr_external_provider::ValidateLeases(frame));

    frame.lease_flags = static_cast<std::uint32_t>(NrExternalLeaseFlags::OutputSameAsColor);
    frame.output.resource = 0x2999;
    CHECK(!nr_external_provider::ValidateLeases(frame));

    // Competing NR blocks claim.
    nr_external_provider::Session other;
    cfg.competing_nr = true;
    CHECK(!nr_external_provider::ClaimOwnership(other, cfg, kNrExternalProviderIdDl5f));

    // Prefer Native over Synthetic.
    CHECK(nr_external_provider::PreferProvenance(
        NrGuideProvenance::Synthetic, NrGuideProvenance::Native) ==
        NrGuideProvenance::Native);

    NrExternalSubmitResultV1 result{};
    nr_external_provider::FillSubmitResult(first, MakeFrame(), true, result);
    CHECK(result.writeback_performed == 1);
    CHECK(result.bypass[0] != 0);

    CHECK(NrExternalProviderReasonName(
              NrExternalProviderReason::RuntimeUnavailable)[0] == 'r');

    {
        nr_external_provider_feeder::FeedInputs in{};
        in.command_list = 1;
        in.color = 2;
        in.output = 3;
        in.depth = 4;
        in.motion = 5;
        in.width = 64;
        in.height = 32;
        in.rect_x = 4;
        in.rect_y = 2;
        in.rect_w = 60;
        in.rect_h = 28;
        in.pre_exposure = 1.25f;
        in.frame_id = 9;
        in.epoch = 3;
        in.view_id = 1;
        in.queue = 6;
        in.fence = 7;
        NrExternalFrameV1 mapped{};
        CHECK(nr_external_provider_feeder::BuildSyntheticFrame(in, mapped));
        CHECK(mapped.color.rect_x == 4 && mapped.color.rect_w == 60);
        CHECK(mapped.pre_exposure == 1.25f);
        CHECK(mapped.frame_id == 9 && mapped.epoch == 3);
        CHECK(mapped.depth.resource == 4 && mapped.motion.resource == 5);
        in.depth_inverted = true;
        in.hdr_color = true;
        CHECK(nr_external_provider_feeder::BuildSyntheticFrame(in, mapped));
        CHECK((mapped.lease_flags & static_cast<std::uint32_t>(
                  NrExternalLeaseFlags::DepthInverted)) != 0);
        CHECK((mapped.lease_flags & static_cast<std::uint32_t>(
                  NrExternalLeaseFlags::HdrColor)) != 0);
    }

    {
        nr_external_provider::Session roll;
        nr_external_provider::ConfigSnapshot cfg2;
        cfg2.master_enabled = true;
        cfg2.transfer_strength = 1.0f;
        cfg2.observer_epoch = 2;
        CHECK(nr_external_provider::ClaimOwnership(roll, cfg2, kNrExternalProviderIdDl5f));
        auto f = MakeFrame();
        f.frame_id = 42;
        const auto before = roll;
        auto a = nr_external_provider::Admit(roll, cfg2, f);
        CHECK(a.admit);
        CHECK(roll.last_frame_id == 42);
        nr_external_provider::RollbackAdmit(
            roll, before, NrExternalProviderReason::LeaseInvalid);
        CHECK(roll.last_frame_id == 0);
        CHECK(roll.active_view_id == 0 && roll.active_epoch == 0);
        CHECK(roll.last_bypass == NrExternalProviderReason::LeaseInvalid);
    }

    std::cout << checks
              << " MC1 external-provider ABI/admission/provenance assertions passed "
                 "(host CPU; no NVIDIA/game claim)\n";
}
