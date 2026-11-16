#include "instrument/instrument.hpp"
#include <gtest/gtest.h>
#include <limits>

TEST(InstrumentTest, EquityCreation) {
    Equity equity("EQ-001", "AAPL");
    EXPECT_EQ(equity.instrumentID(), "EQ-001");
    EXPECT_EQ(equity.ticker(), "AAPL");
}

TEST(InstrumentTest, EquityCreationInvalidParametersThrows) {
    EXPECT_THROW(Equity("", "AAPL"), std::invalid_argument);
    EXPECT_THROW(Equity("EQ-001", ""), std::invalid_argument);
}

TEST(InstrumentTest, EuropeanOptionCreation) {
    EuropeanOption option(
        "OPT-001", "EQ-001", 100.0,
        std::chrono::sys_days{std::chrono::year{2024}/6/1},
        std::chrono::sys_days{std::chrono::year{2024}/12/1},
        OptionType::CALL,
        100
    );

    EXPECT_EQ(option.instrumentID(), "OPT-001");
    EXPECT_EQ(option.underlyingID(), "EQ-001");
    EXPECT_DOUBLE_EQ(option.strike(), 100.0);
    EXPECT_EQ(option.startDate(), std::chrono::sys_days{std::chrono::year{2024}/6/1});
    EXPECT_EQ(option.expiryDate(), std::chrono::sys_days{std::chrono::year{2024}/12/1});
    EXPECT_EQ(option.optionType(), OptionType::CALL);
    EXPECT_DOUBLE_EQ(option.contractMultiplier(), 100.0);
}

TEST(InstrumentTest, EuropeanOptionCreationInvalidParametersThrows) {
    std::chrono::sys_days start_date = std::chrono::sys_days{std::chrono::year{2024}/6/1};
    std::chrono::sys_days end_date = std::chrono::sys_days{std::chrono::year{2024}/12/1};
    double strike = 100.0;

    EXPECT_THROW(
        EuropeanOption(
            "", "EQ-001", strike,
            start_date,
            end_date,
            OptionType::CALL,
            100.
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        EuropeanOption(
            "OPT-001", "", strike,
            start_date,
            end_date,
            OptionType::CALL,
            100.
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        EuropeanOption(
            "OPT-001", "EQ-001", -strike,
            start_date,
            end_date,
            OptionType::CALL,
            100.
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        EuropeanOption(
            "OPT-001", "EQ-001", std::numeric_limits<double>::infinity(),
            start_date,
            end_date,
            OptionType::CALL,
            100.
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        EuropeanOption(
            "OPT-001", "EQ-001", std::numeric_limits<double>::quiet_NaN(),
            start_date,
            end_date,
            OptionType::CALL,
            100.
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        EuropeanOption(
            "OPT-001", "EQ-001", 100.0,
            end_date,
            start_date,
            OptionType::CALL,
            100.
        ),
        std::invalid_argument
    );
}

TEST(InstrumentTest, FutureCreation) {
    Future future(
        "FUT-001", "EQ-001", 100.0,
        std::chrono::sys_days{std::chrono::year{2024}/6/1},
        std::chrono::sys_days{std::chrono::year{2024}/12/1}
    );

    EXPECT_EQ(future.instrumentID(), "FUT-001");
    EXPECT_EQ(future.underlyingID(), "EQ-001");
    EXPECT_DOUBLE_EQ(future.contractMultiplier(), 100.0);
    EXPECT_EQ(future.startDate(), std::chrono::sys_days{std::chrono::year{2024}/6/1});
    EXPECT_EQ(future.expiryDate(), std::chrono::sys_days{std::chrono::year{2024}/12/1});
}

TEST(InstrumentTest, FutureCreationInvalidParametersThrows) {
    EXPECT_THROW(
        Future(
            "", "EQ-001", 100.0,
            std::chrono::sys_days{std::chrono::year{2024}/6/1},
            std::chrono::sys_days{std::chrono::year{2024}/12/1}
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        Future(
            "FUT-001", "", 100.0,
            std::chrono::sys_days{std::chrono::year{2024}/6/1},
            std::chrono::sys_days{std::chrono::year{2024}/12/1}
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        Future(
            "FUT-001", "EQ-001", -100.0,
            std::chrono::sys_days{std::chrono::year{2024}/6/1},
            std::chrono::sys_days{std::chrono::year{2024}/12/1}
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        Future(
            "FUT-001", "EQ-001", 100.0,
            std::chrono::sys_days{std::chrono::year{2024}/12/1},
            std::chrono::sys_days{std::chrono::year{2024}/6/1}
        ),
        std::invalid_argument
    );
}

// Migrated from the former valuation/test_contract_multiplier.cpp: the
// multiplier lives on the instrument, so its validation belongs here.
TEST(InstrumentTest, ContractMultipliersAreExposed) {
    const std::chrono::sys_days start{std::chrono::year{2024}/1/1};
    const std::chrono::sys_days expiry{std::chrono::year{2025}/1/1};

    EXPECT_DOUBLE_EQ(Equity("EQ-001", "AAPL").contractMultiplier(), 1.0);

    EXPECT_DOUBLE_EQ(
        EuropeanOption(
            "OPT-001", "EQ-001", 100.0, start, expiry,
            OptionType::CALL, 25.0
        ).contractMultiplier(),
        25.0
    );

    EXPECT_DOUBLE_EQ(
        Future("FUT-001", "EQ-001", 50.0, start, expiry)
            .contractMultiplier(),
        50.0
    );
}

TEST(InstrumentTest, InvalidContractMultiplierThrows) {
    const std::chrono::sys_days start{std::chrono::year{2024}/1/1};
    const std::chrono::sys_days expiry{std::chrono::year{2025}/1/1};
    const double infinity = std::numeric_limits<double>::infinity();

    for (double multiplier : {0.0, -1.0, infinity, -infinity,
                              std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_THROW(
            (EuropeanOption{
                "OPT-001", "EQ-001", 100.0, start, expiry,
                OptionType::CALL, multiplier
            }),
            std::invalid_argument
        );
        EXPECT_THROW(
            (Future{"FUT-001", "EQ-001", multiplier, start, expiry}),
            std::invalid_argument
        );
    }
}
