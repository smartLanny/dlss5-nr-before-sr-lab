// Ordinary Present owns event registration. The drawing callback only records
// completion, so it never changes the callback list currently being traversed.
nr_face_diagnostic_ui::View FaceSelectionView(reshade::api::effect_runtime *runtime);
void FocusFaceDiagnosticPage();
void DrawFaceRegionSelection(reshade::api::effect_runtime *runtime);

struct FaceSelectionState {
    reshade::api::effect_runtime *owner = nullptr; // Identity only; never dereferenced from storage.
    nr_face_diagnostic_ui::Selection selection{};
    unsigned long long generation = 0;
    bool requested = false, registered = false, restore_panel = false;
};
SRWLOCK g_face_selection_lock = SRWLOCK_INIT;
FaceSelectionState g_face_selection;

FaceSelectionState SnapshotFaceRegionSelection() {
    AcquireSRWLockShared(&g_face_selection_lock);
    const auto snapshot = g_face_selection;
    ReleaseSRWLockShared(&g_face_selection_lock);
    return snapshot;
}
void StoreFaceRegionPreference(const nr_face_diagnostic_ui::Selection &selection,
    unsigned long long expected_generation) {
    AcquireSRWLockExclusive(&g_face_selection_lock);
    if (g_face_selection.generation == expected_generation && !g_face_selection.requested &&
        !g_face_selection.registered && !g_face_selection.restore_panel)
        g_face_selection.selection = selection;
    ReleaseSRWLockExclusive(&g_face_selection_lock);
}
bool QueueFaceRegionSelection(reshade::api::effect_runtime *runtime,
    nr_face_diagnostic_ui::Selection selection) {
    if (!runtime) return false;
    AcquireSRWLockExclusive(&g_face_selection_lock);
    const bool accepted = !g_face_selection.requested && !g_face_selection.registered &&
        !g_face_selection.restore_panel;
    if (accepted) {
        selection.saved_x = selection.center_x; selection.saved_y = selection.center_y;
        selection.picking = true;
        g_face_selection.owner = runtime; g_face_selection.selection = selection;
        ++g_face_selection.generation; g_face_selection.requested = true;
    }
    ReleaseSRWLockExclusive(&g_face_selection_lock);
    return accepted;
}
void FinishFaceRegionSelection(reshade::api::effect_runtime *runtime,
    unsigned long long expected_generation, nr_face_diagnostic_ui::Selection selection,
    bool restore_panel) {
    AcquireSRWLockExclusive(&g_face_selection_lock);
    if (g_face_selection.owner == runtime && g_face_selection.generation == expected_generation &&
        g_face_selection.requested) {
        selection.picking = false;
        g_face_selection.selection = selection; g_face_selection.requested = false;
        g_face_selection.restore_panel = restore_panel; ++g_face_selection.generation;
    }
    ReleaseSRWLockExclusive(&g_face_selection_lock);
}
void ServiceFaceDiagnosticSelection(reshade::api::effect_runtime *runtime) {
    bool close_panel = false, restore_panel = false;
    AcquireSRWLockExclusive(&g_face_selection_lock);
    if (g_face_selection.requested && g_face_selection.owner == runtime && !g_face_selection.registered) {
        reshade::register_event<reshade::addon_event::reshade_overlay>(DrawFaceRegionSelection);
        g_face_selection.registered = true; close_panel = true;
    } else if (!g_face_selection.requested && g_face_selection.registered) {
        reshade::unregister_event<reshade::addon_event::reshade_overlay>(DrawFaceRegionSelection);
        g_face_selection.registered = false;
    }
    if (!g_face_selection.requested && !g_face_selection.registered && g_face_selection.restore_panel &&
        g_face_selection.owner == runtime) {
        restore_panel = true; g_face_selection.restore_panel = false;
    }
    ReleaseSRWLockExclusive(&g_face_selection_lock);
    if (close_panel && runtime) {
        runtime->block_input_next_frame();
        runtime->open_overlay(false, reshade::api::input_source::mouse);
    }
    if (restore_panel && runtime) {
        FocusFaceDiagnosticPage();
        runtime->open_overlay(true, reshade::api::input_source::mouse);
    }
}
void CleanupFaceDiagnosticSelection(reshade::api::effect_runtime *runtime) {
    AcquireSRWLockExclusive(&g_face_selection_lock);
    if (!runtime || g_face_selection.owner == runtime) {
        if (g_face_selection.registered)
            reshade::unregister_event<reshade::addon_event::reshade_overlay>(DrawFaceRegionSelection);
        g_face_selection.owner = nullptr;
        g_face_selection.requested = g_face_selection.registered = g_face_selection.restore_panel = false;
        g_face_selection.selection.picking = false; ++g_face_selection.generation;
    }
    ReleaseSRWLockExclusive(&g_face_selection_lock);
}
void DrawFaceRegionSelection(reshade::api::effect_runtime *runtime) {
    const auto snapshot = SnapshotFaceRegionSelection();
    if (!runtime || !snapshot.requested || !snapshot.registered || snapshot.owner != runtime) return;
    auto selection = snapshot.selection;
    const auto view = FaceSelectionView(runtime);
    if (!view.enabled || view.blocked || !view.geometry.valid) {
        selection.center_x = selection.saved_x; selection.center_y = selection.saved_y;
        FinishFaceRegionSelection(runtime, snapshot.generation, selection, !view.blocked);
        return;
    }
    runtime->block_input_next_frame();
    ImDrawList *draw = ImGui::GetForegroundDrawList(nullptr);
    nr_face_diagnostic_ui::PickRegion(selection, view, draw,
        draw->GetClipRectMin(), draw->GetClipRectMax(), UseChineseUi());
    if (!selection.picking) {
        FinishFaceRegionSelection(runtime, snapshot.generation, selection, true);
        return;
    }
    AcquireSRWLockExclusive(&g_face_selection_lock);
    if (g_face_selection.owner == runtime && g_face_selection.generation == snapshot.generation &&
        g_face_selection.requested)
        g_face_selection.selection = selection;
    ReleaseSRWLockExclusive(&g_face_selection_lock);
}
