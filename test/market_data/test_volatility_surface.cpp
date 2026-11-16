#include <gtest/gtest.h>
#include <limits>
#include "volatility/flat_volatility_surface.hpp"


TEST(VolaSurfaceTest, FlatSurfaceReturnsExpectedVola) {
    const double volatility = 100.0;
    FlatVolSurface vola_surface{volatility};

    EXPECT_DOUBLE_EQ(vola_surface.impliedVolatility(100.0,1.0), volatility);
    EXPECT_DOUBLE_EQ(vola_surface.impliedVolatility(90.0,1.0), volatility);
    EXPECT_DOUBLE_EQ(vola_surface.impliedVolatility(100.0,2.0), volatility);
    EXPECT_DOUBLE_EQ(vola_surface.impliedVolatility(90.,2.), volatility);
}

TEST(VolaSurfaceTest, FlatSurfaceBadParamsThrow) {
    const double vola = 100.;
    const double vola_inf = std::numeric_limits<double>::infinity();
    const double vola_nan = std::numeric_limits<double>::quiet_NaN();
    
    EXPECT_THROW(FlatVolSurface{vola_nan}, std::invalid_argument);
    EXPECT_THROW(FlatVolSurface{vola_inf}, std::invalid_argument);
    EXPECT_THROW(FlatVolSurface{-vola}, std::invalid_argument);

    const double strike_inf = std::numeric_limits<double>::infinity();
    const double strike_nan = std::numeric_limits<double>::quiet_NaN();
    const double strike_invalid = -100.;

    FlatVolSurface vola_surface = FlatVolSurface{vola};
    EXPECT_THROW(vola_surface.impliedVolatility(strike_inf, 1.0), std::invalid_argument);
    EXPECT_THROW(vola_surface.impliedVolatility(strike_nan, 1.0), std::invalid_argument);
    EXPECT_THROW(vola_surface.impliedVolatility(strike_invalid, 1.0), std::invalid_argument);
}
