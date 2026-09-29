#pragma once
// Minimal dependency-free test helper. Each test file defines a main() that calls
// SCX_RUN(testFunction) and returns scxTestExitCode(). CTest treats a non-zero
// exit code as a failure.

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace scx_test {
inline int& failureCount() { static int failures = 0; return failures; }
inline int& checkCount() { static int checks = 0; return checks; }
}  // namespace scx_test

#define SCX_CHECK(condition)                                                              \
    do {                                                                                  \
        ++scx_test::checkCount();                                                         \
        if (!(condition)) {                                                               \
            ++scx_test::failureCount();                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << "  CHECK FAILED: " #condition "\n"; \
        }                                                                                 \
    } while (0)

#define SCX_CHECK_EQ(actual, expected)                                                    \
    do {                                                                                  \
        ++scx_test::checkCount();                                                         \
        const auto actualValue = (actual);                                                \
        const auto expectedValue = (expected);                                            \
        if (!(actualValue == expectedValue)) {                                            \
            ++scx_test::failureCount();                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << "  CHECK_EQ FAILED: " #actual    \
                      << " = " << actualValue << ", expected " << expectedValue << "\n";  \
        }                                                                                 \
    } while (0)

// Passes only if `expression` throws exceptionType.
#define SCX_CHECK_THROWS(expression, exceptionType)                                       \
    do {                                                                                  \
        ++scx_test::checkCount();                                                         \
        bool threwExpected = false;                                                       \
        try { (void)(expression); } catch (const exceptionType&) { threwExpected = true; } \
        if (!threwExpected) {                                                             \
            ++scx_test::failureCount();                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << "  expected " #exceptionType " from " #expression "\n"; \
        }                                                                                 \
    } while (0)

#define SCX_RUN(testFunction)                                                             \
    do {                                                                                  \
        const int failuresBefore = scx_test::failureCount();                              \
        try { testFunction(); }                                                           \
        catch (const std::exception& error) {                                             \
            ++scx_test::failureCount();                                                   \
            std::cerr << #testFunction " threw: " << error.what() << "\n";                \
        }                                                                                 \
        std::cout << (scx_test::failureCount() == failuresBefore ? "[ PASS ] " : "[ FAIL ] ") \
                  << #testFunction << std::endl;                                          \
    } while (0)

inline int scxTestExitCode() {
    std::cout << scx_test::checkCount() << " checks, " << scx_test::failureCount() << " failures\n";
    return scx_test::failureCount() == 0 ? 0 : 1;
}
