#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <tatra/desktop/io.hpp>
#include <tatra/validation.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tatra::desktop {
namespace {

constexpr double kMarkerRadius = 4.0;
constexpr double kLabelHeight = 24.0;
constexpr double kLabelPadding = 5.0;
constexpr double kLabelGap = 7.0;
constexpr double kCanvasMargin = 4.0;

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

enum class SvgStatus { Visible, Occluded, Uncertain, MissingTerrain, NotEvaluated };

struct RenderedPeak {
    const Peak* peak = nullptr;
    PixelPoint marker;
    SvgStatus status = SvgStatus::NotEvaluated;
    std::string label;
};

struct LabelPlacement {
    const RenderedPeak* rendered_peak = nullptr;
    std::optional<Rect> box;
};

std::string escapeXml(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

std::size_t utf8CharacterCount(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](unsigned char byte) {
        return (byte & 0xc0U) != 0x80U;
    }));
}

std::string number(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

std::string elevationText(const Peak& peak) {
    if (!peak.display_elevation)
        return "wysokość: brak danych";
    return number(peak.display_elevation->metres) + " m";
}

bool overlaps(const Rect& first, const Rect& second) {
    constexpr double separation = 3.0;
    return first.x < second.x + second.width + separation &&
           first.x + first.width + separation > second.x &&
           first.y < second.y + second.height + separation &&
           first.y + first.height + separation > second.y;
}

Rect insideCanvas(Rect box, double canvas_width, double canvas_height) {
    box.x = std::clamp(box.x, kCanvasMargin,
                       std::max(kCanvasMargin, canvas_width - box.width - kCanvasMargin));
    box.y = std::clamp(box.y, kCanvasMargin,
                       std::max(kCanvasMargin, canvas_height - box.height - kCanvasMargin));
    return box;
}

std::vector<LabelPlacement> layoutLabels(const std::vector<RenderedPeak>& peaks, int width_px,
                                         int height_px) {
    const double canvas_width = static_cast<double>(width_px);
    const double canvas_height = static_cast<double>(height_px);
    std::vector<Rect> occupied;
    std::vector<LabelPlacement> placements;
    placements.reserve(peaks.size());

    for (const auto& peak : peaks) {
        const double estimated_text_width =
            std::max(52.0, static_cast<double>(utf8CharacterCount(peak.label)) * 7.5);
        const double width = std::min(estimated_text_width + 2.0 * kLabelPadding,
                                      std::max(1.0, canvas_width - 2.0 * kCanvasMargin));
        if (canvas_height < kLabelHeight + 2.0 * kCanvasMargin) {
            placements.push_back({&peak, std::nullopt});
            continue;
        }

        const std::array<Rect, 4> local_candidates{{
            {peak.marker.u + kLabelGap, peak.marker.v - kLabelHeight - kLabelGap, width,
             kLabelHeight},
            {peak.marker.u + kLabelGap, peak.marker.v + kLabelGap, width, kLabelHeight},
            {peak.marker.u - width - kLabelGap, peak.marker.v - kLabelHeight - kLabelGap, width,
             kLabelHeight},
            {peak.marker.u - width - kLabelGap, peak.marker.v + kLabelGap, width, kLabelHeight},
        }};

        std::vector<Rect> candidates;
        candidates.reserve(local_candidates.size() + static_cast<std::size_t>(height_px / 28 + 1));
        for (const auto& candidate : local_candidates) {
            const auto clamped = insideCanvas(candidate, canvas_width, canvas_height);
            if (std::none_of(candidates.begin(), candidates.end(), [&](const Rect& existing) {
                    return existing.x == clamped.x && existing.y == clamped.y;
                }))
                candidates.push_back(clamped);
        }
        for (double y = kCanvasMargin; y + kLabelHeight <= canvas_height - kCanvasMargin;
             y += kLabelHeight + 4.0) {
            candidates.push_back(insideCanvas(
                {peak.marker.u < canvas_width / 2.0 ? peak.marker.u + kLabelGap
                                                    : peak.marker.u - width - kLabelGap,
                 y, width, kLabelHeight},
                canvas_width, canvas_height));
        }

        const auto available = std::find_if(candidates.begin(), candidates.end(), [&](const Rect& box) {
            return std::none_of(occupied.begin(), occupied.end(),
                                [&](const Rect& used) { return overlaps(box, used); });
        });
        if (available == candidates.end()) {
            placements.push_back({&peak, std::nullopt});
        } else {
            occupied.push_back(*available);
            placements.push_back({&peak, *available});
        }
    }
    return placements;
}

SvgStatus svgStatus(const PeakObservation& observation) {
    if (!observation.visibility)
        return SvgStatus::NotEvaluated;
    switch (observation.visibility->state) {
    case VisibilityState::VisibleInModel:
        return SvgStatus::Visible;
    case VisibilityState::OccludedInModel:
        return SvgStatus::Occluded;
    case VisibilityState::Uncertain:
        return SvgStatus::Uncertain;
    case VisibilityState::MissingTerrain:
        return SvgStatus::MissingTerrain;
    }
    throw Error{ErrorCode::InvalidInput, "Unknown visibility state"};
}

std::string_view cssClass(SvgStatus status) {
    switch (status) {
    case SvgStatus::Visible:
        return "status-visible";
    case SvgStatus::Occluded:
        return "status-occluded";
    case SvgStatus::Uncertain:
        return "status-uncertain";
    case SvgStatus::MissingTerrain:
        return "status-missing-terrain";
    case SvgStatus::NotEvaluated:
        return "status-not-evaluated";
    }
    throw Error{ErrorCode::InvalidInput, "Unknown SVG status"};
}

bool renderInMode(const PeakObservation& observation, SvgStatus status, bool debug) {
    if (!observation.projection.pixel)
        return false;
    if (debug)
        return true;
    return observation.projection.state == ProjectionState::InFrame &&
           status != SvgStatus::Occluded && status != SvgStatus::NotEvaluated;
}

void validateProjection(const PeakObservation& observation) {
    switch (observation.projection.state) {
    case ProjectionState::InFrame:
    case ProjectionState::OutsideFrame:
        if (!observation.projection.pixel)
            throw Error{ErrorCode::InvalidInput, "Projected peak has no pixel: " + observation.id};
        break;
    case ProjectionState::BehindNearPlane:
        if (observation.projection.pixel)
            throw Error{ErrorCode::InvalidInput,
                        "Peak behind near plane must not have a pixel: " + observation.id};
        break;
    default:
        throw Error{ErrorCode::InvalidInput, "Unknown projection state"};
    }
    if (observation.projection.pixel &&
        (!std::isfinite(observation.projection.pixel->u) ||
         !std::isfinite(observation.projection.pixel->v)))
        throw Error{ErrorCode::InvalidInput, "Peak projection pixel must be finite: " + observation.id};
}

void writeLegend(std::ostringstream& svg, int width_px, bool debug) {
    struct Entry {
        const char* css_class;
        const char* label;
    };
    constexpr std::array<Entry, 4> entries{{
        {"status-visible", "VisibleInModel"},
        {"status-uncertain", "Uncertain"},
        {"status-missing-terrain", "MissingTerrain"},
        {"status-occluded", "OccludedInModel (debug)"},
    }};
    const double legend_width = 216.0;
    const double x = std::max(kCanvasMargin, static_cast<double>(width_px) - legend_width - 8.0);
    const double height = debug ? 114.0 : 92.0;
    svg << "  <g class=\"legend\" transform=\"translate(" << number(x)
        << " 8)\" aria-label=\"Legenda statusów\">\n"
        << "    <rect class=\"legend-background\" width=\"" << number(legend_width)
        << "\" height=\"" << number(height) << "\" rx=\"5\"/>\n"
        << "    <text class=\"legend-title\" x=\"10\" y=\"18\">Widoczność</text>\n";
    std::size_t visible_index = 0;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (index == 3 && !debug)
            continue;
        const double y = 38.0 + static_cast<double>(visible_index) * 22.0;
        svg << "    <circle class=\"peak-marker " << entries[index].css_class
            << "\" cx=\"14\" cy=\"" << number(y - 4.0) << "\" r=\"4\"/>\n"
            << "    <text x=\"26\" y=\"" << number(y) << "\">" << entries[index].label
            << "</text>\n";
        ++visible_index;
    }
    svg << "  </g>\n";
}

} // namespace

Status writeOverlaySvg(const FrameResult& frame, const PeakCatalog& catalog,
                       const RenderOptions& options, const std::filesystem::path& output) {
    try {
        const auto camera = validateCamera(options.camera);
        if (const auto* error = std::get_if<Error>(&camera))
            return *error;

        std::unordered_map<PeakId, const Peak*> catalog_by_id;
        for (const auto& peak : catalog.peaks) {
            if (!catalog_by_id.emplace(peak.id, &peak).second)
                return Error{ErrorCode::InvalidInput, "Ambiguous duplicate peak id: " + peak.id};
            if (peak.display_elevation && !std::isfinite(peak.display_elevation->metres))
                return Error{ErrorCode::InvalidInput,
                             "Catalog display elevation must be finite: " + peak.id};
        }

        std::vector<RenderedPeak> rendered_peaks;
        rendered_peaks.reserve(frame.observations.size());
        for (const auto& observation : frame.observations) {
            validateProjection(observation);
            const auto found = catalog_by_id.find(observation.id);
            if (found == catalog_by_id.end())
                return Error{ErrorCode::InvalidInput,
                             "Unknown peak id in result: " + observation.id};
            const auto status = svgStatus(observation);
            if (!renderInMode(observation, status, options.debug_visibility))
                continue;
            const auto& peak = *found->second;
            rendered_peaks.push_back(
                {&peak, *observation.projection.pixel, status,
                 peak.name + " — " + elevationText(peak)});
        }

        const auto placements =
            layoutLabels(rendered_peaks, options.camera.width_px, options.camera.height_px);
        std::ostringstream svg;
        svg.imbue(std::locale::classic());
        svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\""
            << " width=\"" << options.camera.width_px << "\" height=\"" << options.camera.height_px
            << "\" viewBox=\"0 0 " << options.camera.width_px << ' ' << options.camera.height_px
            << "\" data-debug=\"" << (options.debug_visibility ? "true" : "false") << "\">\n"
            << "  <title>TatraPeak overlay</title>\n"
            << "  <style>\n"
            << "    text { font-family: sans-serif; font-size: 13px; fill: #fff; }\n"
            << "    .peak-marker { stroke: #111; stroke-width: 1.5; }\n"
            << "    .peak-label rect { fill: #111; fill-opacity: .82; stroke-width: 2; }\n"
            << "    .leader { fill: none; stroke-width: 1.5; }\n"
            << "    .status-visible { fill: #35c759; stroke: #35c759; }\n"
            << "    .status-uncertain { fill: #ffd60a; stroke: #ffd60a; }\n"
            << "    .status-missing-terrain { fill: #ff9f0a; stroke: #ff9f0a; stroke-dasharray: 3 2; }\n"
            << "    .status-occluded { fill: #ff453a; stroke: #ff453a; }\n"
            << "    .status-not-evaluated { fill: #8e8e93; stroke: #8e8e93; }\n"
            << "    .peak-label text { fill: #fff; stroke: none; }\n"
            << "    .legend-background { fill: #111; fill-opacity: .86; stroke: #ddd; }\n"
            << "    .legend-title { font-weight: bold; }\n"
            << "  </style>\n";

        if (options.background_image) {
            svg << "  <image class=\"background\" x=\"0\" y=\"0\" width=\""
                << options.camera.width_px << "\" height=\"" << options.camera.height_px
                << "\" preserveAspectRatio=\"none\" href=\""
                << escapeXml(options.background_image->generic_string()) << "\" xlink:href=\""
                << escapeXml(options.background_image->generic_string()) << "\"/>\n";
        } else {
            svg << "  <rect class=\"canvas-background\" width=\"100%\" height=\"100%\" fill=\"#18212b\"/>\n";
        }

        for (const auto& placement : placements) {
            if (!placement.box)
                continue;
            const auto& peak = *placement.rendered_peak;
            const auto& box = *placement.box;
            const double anchor_x = std::clamp(peak.marker.u, box.x, box.x + box.width);
            const double anchor_y = std::clamp(peak.marker.v, box.y, box.y + box.height);
            svg << "  <line class=\"leader " << cssClass(peak.status) << "\" data-peak-id=\""
                << escapeXml(peak.peak->id) << "\" x1=\"" << number(peak.marker.u) << "\" y1=\""
                << number(peak.marker.v) << "\" x2=\"" << number(anchor_x) << "\" y2=\""
                << number(anchor_y) << "\"/>\n"
                << "  <g class=\"peak-label " << cssClass(peak.status) << "\" data-peak-id=\""
                << escapeXml(peak.peak->id) << "\" transform=\"translate(" << number(box.x) << ' '
                << number(box.y) << ")\">\n"
                << "    <rect width=\"" << number(box.width) << "\" height=\""
                << number(box.height) << "\" rx=\"4\"/>\n"
                << "    <text x=\"" << number(kLabelPadding) << "\" y=\"16\">"
                << escapeXml(peak.label) << "</text>\n"
                << "  </g>\n";
        }
        for (const auto& peak : rendered_peaks) {
            svg << "  <circle class=\"peak-marker " << cssClass(peak.status)
                << "\" data-peak-id=\"" << escapeXml(peak.peak->id) << "\" cx=\""
                << number(peak.marker.u) << "\" cy=\"" << number(peak.marker.v) << "\" r=\""
                << number(kMarkerRadius) << "\"/>\n";
        }
        writeLegend(svg, options.camera.width_px, options.debug_visibility);
        svg << "</svg>\n";

        const auto serialized = std::move(svg).str();
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(output, std::ios::binary | std::ios::trunc);
        stream << serialized;
        stream.close();
        return std::monostate{};
    } catch (const Error& error) {
        return error;
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure, "Cannot write SVG overlay: " + std::string{error.what()}};
    }
}

} // namespace tatra::desktop
