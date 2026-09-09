// Reuse the native binding's scale/diagnostic representation for source-pixel
// external guides. Native/Extracted provenance promises supplied jitter (zero
// included); legacy Synthetic zero remains unspecified, never invented input.
inline NrJitterBinding ResolveExternalJitter(const NrExternalResourceV1 &color,
                                            NrJitterMode mode,
                                            unsigned source_width, unsigned source_height,
                                            unsigned work_width, unsigned work_height) {
    auto result = ResolveNrJitter(nullptr, mode, source_width, source_height,
                                 work_width, work_height);
    const bool supplied = color.provenance == NrGuideProvenance::Native ||
        color.provenance == NrGuideProvenance::Extracted ||
        (color.provenance == NrGuideProvenance::Synthetic &&
         (color.jitter_x != 0.0f || color.jitter_y != 0.0f));
    if (!supplied) return result;
    result.source_x = color.jitter_x;
    result.source_y = color.jitter_y;
    const float limit = static_cast<float>(std::max(1u, std::max(source_width, source_height)));
    result.valid_x = std::isfinite(color.jitter_x) && std::fabs(color.jitter_x) <= limit;
    result.valid_y = std::isfinite(color.jitter_y) && std::fabs(color.jitter_y) <= limit;
    result.known = mode == NrJitterMode::GameInputPixel && result.valid_x && result.valid_y &&
        source_width && source_height && work_width && work_height &&
        std::isfinite(result.scale_x) && std::isfinite(result.scale_y) &&
        result.scale_x > 0.0f && result.scale_y > 0.0f;
    if (result.known) {
        result.x = color.jitter_x * result.scale_x;
        result.y = color.jitter_y * result.scale_y;
        result.known = std::isfinite(result.x) && std::isfinite(result.y);
    }
    return result;
}
