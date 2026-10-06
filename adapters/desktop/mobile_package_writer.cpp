#include "file_hash.hpp"

#include <tatra/desktop/io.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>

namespace tatra::desktop {
namespace {
constexpr std::uint64_t maxBytes = 256ULL * 1024 * 1024;
constexpr std::uint64_t maxCells = 40'000'000;
constexpr std::uint32_t maxPeaks = 100'000;
constexpr std::uint32_t maxText = 4096;
constexpr std::streamoff hashOffset = 104;

Error invalid(std::string message) { return {ErrorCode::InvalidInput, std::move(message)}; }
Error io(std::string message) { return {ErrorCode::IoFailure, std::move(message)}; }

template <class T> void little(std::ostream& out, T value) {
    static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
    using Bits = std::conditional_t<sizeof(T) == 8, std::uint64_t, std::uint32_t>;
    Bits bits;
    if constexpr (std::is_floating_point_v<T>)
        bits = std::bit_cast<Bits>(value);
    else
        bits = static_cast<Bits>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        out.put(static_cast<char>((bits >> (8 * i)) & 0xffU));
}

std::array<std::uint8_t, 32> decodeHash(const std::string& text) {
    if (text.size() != 64)
        throw std::runtime_error("Invalid SHA-256 length");
    const auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw std::runtime_error("Invalid SHA-256 character");
    };
    std::array<std::uint8_t, 32> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>((nibble(text[2 * i]) << 4U) | nibble(text[2 * i + 1]));
    return result;
}

std::uint64_t recordBytes(const Peak& peak) {
    return 48 + peak.id.size() + peak.name.size();
}
} // namespace

Status writeMobilePackage(const std::filesystem::path& datasetDirectory,
                          const std::filesystem::path& outputFile) {
    auto loaded = loadDataset(datasetDirectory);
    if (const auto* error = std::get_if<Error>(&loaded)) return *error;
    auto dataset = std::get<Dataset>(std::move(loaded));
    auto rasterResult = readDemGeoTiff(datasetDirectory / "terrain-ellipsoid.tif");
    if (const auto* error = std::get_if<Error>(&rasterResult)) return *error;
    auto raster = std::get<DemGeoTiff>(std::move(rasterResult));
    const auto& geometry = raster.geometry;
    if (geometry.rows == 0 || geometry.columns == 0 || geometry.rows > maxCells / geometry.columns)
        return invalid("Mobile terrain dimensions overflow");
    const auto cells = static_cast<std::uint64_t>(geometry.rows * geometry.columns);
    if (cells > maxCells || raster.height_values_m.size() != cells || raster.valid_mask.size() != cells)
        return invalid("Mobile terrain exceeds limits or has inconsistent arrays");
    if (dataset.catalog.peaks.size() > maxPeaks || dataset.metadata.dataset_id.empty() ||
        dataset.metadata.dataset_id.size() > maxText)
        return invalid("Mobile dataset id or peak count exceeds v1 limits");
    std::uint64_t peaksBytes = 0;
    for (const auto& peak : dataset.catalog.peaks) {
        if (peak.id.empty() || peak.id.size() > maxText || peak.name.size() > maxText)
            return invalid("Mobile peak text exceeds v1 limits: " + peak.id);
        const auto bytes = recordBytes(peak);
        if (peaksBytes > maxBytes - bytes) return invalid("Mobile peak section overflows");
        peaksBytes += bytes;
    }
    const std::uint64_t heightsBytes = cells * sizeof(float);
    const std::uint64_t total = 168 + dataset.metadata.dataset_id.size() + heightsBytes + cells + peaksBytes;
    if (total > maxBytes) return invalid("Mobile package exceeds 256 MiB");
    const auto manifestHash = decodeHash(dataset.metadata.manifest_sha256);
    const auto temporary = outputFile.string() + ".tmp";
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return io("Cannot create mobile package: " + temporary);
        out.write("TATRAPK\0", 8);
        little<std::uint32_t>(out, 1); little<std::uint32_t>(out, 168);
        little<std::uint64_t>(out, total);
        little<std::uint32_t>(out, static_cast<std::uint32_t>(geometry.rows));
        little<std::uint32_t>(out, static_cast<std::uint32_t>(geometry.columns));
        little<double>(out, geometry.first_center_latitude_deg);
        little<double>(out, geometry.first_center_longitude_deg);
        little<double>(out, geometry.latitude_step_deg);
        little<double>(out, geometry.longitude_step_deg);
        little<std::uint32_t>(out, static_cast<std::uint32_t>(dataset.catalog.peaks.size()));
        little<std::uint32_t>(out, 0);
        little<std::uint32_t>(out, static_cast<std::uint32_t>(dataset.metadata.dataset_id.size()));
        little<std::uint32_t>(out, 0);
        little<std::uint64_t>(out, heightsBytes); little<std::uint64_t>(out, cells);
        little<std::uint64_t>(out, peaksBytes);
        const std::array<char, 32> zeros{};
        out.write(zeros.data(), zeros.size());
        out.write(reinterpret_cast<const char*>(manifestHash.data()), manifestHash.size());
        out.write(dataset.metadata.dataset_id.data(), static_cast<std::streamsize>(dataset.metadata.dataset_id.size()));
        for (const auto value : raster.height_values_m) little<float>(out, value);
        out.write(reinterpret_cast<const char*>(raster.valid_mask.data()), static_cast<std::streamsize>(raster.valid_mask.size()));
        for (const auto& peak : dataset.catalog.peaks) {
            little<std::uint32_t>(out, static_cast<std::uint32_t>(peak.id.size()));
            little<std::uint32_t>(out, static_cast<std::uint32_t>(peak.name.size()));
            little<double>(out, peak.geometry_position.coord.latitude_deg);
            little<double>(out, peak.geometry_position.coord.longitude_deg);
            little<double>(out, peak.geometry_position.ellipsoid_height_m);
            out.put(peak.display_elevation ? 1 : 0);
            const std::array<char, 7> reserved{};
            out.write(reserved.data(), reserved.size());
            little<double>(out, peak.display_elevation ? peak.display_elevation->metres : 0.0);
            out.write(peak.id.data(), static_cast<std::streamsize>(peak.id.size()));
            out.write(peak.name.data(), static_cast<std::streamsize>(peak.name.size()));
        }
        out.close();
        if (!out) return io("Cannot finish mobile package: " + temporary);
        auto hashResult = detail::sha256File(temporary);
        if (const auto* error = std::get_if<Error>(&hashResult)) return *error;
        const auto hash = decodeHash(std::get<std::string>(hashResult));
        std::fstream patch(temporary, std::ios::binary | std::ios::in | std::ios::out);
        patch.seekp(hashOffset); patch.write(reinterpret_cast<const char*>(hash.data()), hash.size()); patch.close();
        if (!patch) return io("Cannot write mobile package hash: " + temporary);
        std::error_code error;
        std::filesystem::rename(temporary, outputFile, error);
        if (error) return io("Cannot publish mobile package: " + error.message());
        return std::monostate{};
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return io("Cannot write mobile package: " + std::string{error.what()});
    }
}
} // namespace tatra::desktop
