#include <tatra/catalog.hpp>

#include <gtest/gtest.h>
#include <limits>
#include <utility>

namespace {
using namespace tatra;

Peak validPeak(PeakId id) {
    Peak peak;
    peak.id = std::move(id);
    peak.name = "fixture";
    peak.geometry_position = {{49.2, 20.1}, 2100.0};
    peak.geometry_height_source = GeometryHeightSource::VerifiedEllipsoid;
    return peak;
}

TEST(PeakCatalog, AcceptsEmptyAndValidCatalogs) {
    EXPECT_TRUE(std::holds_alternative<std::monostate>(validateCatalog({})));
    PeakCatalog catalog{{validPeak("a"), validPeak("b")}};
    catalog.peaks[1].display_elevation = {2000.0, "fixture", "unit test"};
    EXPECT_TRUE(std::holds_alternative<std::monostate>(validateCatalog(catalog)));
}

TEST(PeakCatalog, RejectsEmptyDuplicateAndInvalidRecordsWithContext) {
    for (int variant = 0; variant < 4; ++variant) {
        PeakCatalog catalog{{validPeak("a"), validPeak("b")}};
        if (variant == 0)
            catalog.peaks[1].id.clear();
        else if (variant == 1)
            catalog.peaks[1].id = "a";
        else if (variant == 2)
            catalog.peaks[1].geometry_position.coord.latitude_deg = 91.0;
        else {
            catalog.peaks[1].display_elevation = {std::numeric_limits<double>::quiet_NaN(),
                                                  "fixture", "unit test"};
        }
        const auto result = validateCatalog(catalog);
        ASSERT_TRUE(std::holds_alternative<Error>(result));
        EXPECT_EQ(std::get<Error>(result).code, ErrorCode::InvalidInput);
        EXPECT_FALSE(std::get<Error>(result).message.empty());
    }
}
} // namespace
