cmake_minimum_required(VERSION 3.20)
project(trader VERSION 1.0 LANGUAGES CXX)

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

add_library(trader STATIC
    src/market_data/market_data.cpp
    src/instrument/instrument_registry.cpp
    src/pricing_engine/pricer.cpp
    src/pricing_engine/black_scholes_pricer.cpp
    src/backtesting/market_data/historical_market_data.cpp
    src/backtesting/strategy/strategy_context.cpp
    src/backtesting/strategy/sma_crossover_strategy.cpp
    src/trading/instrument_universe.cpp
    src/trading/position_state.cpp
    src/trading/instrument_trading_spec.cpp
    src/backtesting/account/account.cpp
    src/valuation/account_valuator.cpp
    src/backtesting/engine/performance_metrics.cpp
)

target_include_directories(trader PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(trader PUBLIC cxx_std_20)

FetchContent_Declare(
    magic_enum
    URL https://github.com/Neargye/magic_enum/archive/refs/tags/v0.9.8.zip
    URL_HASH SHA256=16475c3b22250fce2e3a1b344fbc13a549b1259120e86ff60d09917c5ed35394
)
FetchContent_MakeAvailable(magic_enum)
target_link_libraries(trader PRIVATE magic_enum::magic_enum)

set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disabled while fetching Eigen" FORCE)
FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(eigen)
target_link_libraries(trader PUBLIC Eigen3::Eigen)

set(BUILD_TESTING ON CACHE BOOL "Enable creation of trader tests" FORCE)
include(CTest)

if(BUILD_TESTING)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        URL_HASH SHA256=40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(portfolio_tests
        test/market_data/test_market_data.cpp
        test/market_data/test_market_context.cpp
        test/market_data/test_volatility_surface.cpp
        test/pricing/test_pricing_engine.cpp
        test/models/test_black_scholes.cpp
        test/instruments/test_instrument.cpp
        test/instruments/test_instrument_registry.cpp
        test/backtesting/test_historical_market_data.cpp
        test/backtesting/test_backtest_context.cpp
        test/backtesting/test_sma_crossover_strategy.cpp
        test/trading/test_instrument_universe.cpp
        test/trading/test_trading_domain.cpp
        test/trading/test_order_state.cpp
        test/trading/test_pending_order_book.cpp
        test/trading/test_order_generator.cpp
        test/trading/test_instrument_trading_spec.cpp
        test/backtesting/account/test_account.cpp
        test/valuation/test_account_valuation.cpp
        test/backtesting/engine/test_performance_metrics.cpp
        test/backtesting/engine/test_backtest_integration.cpp
    )
    target_link_libraries(portfolio_tests PRIVATE trader GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(portfolio_tests)
endif()
