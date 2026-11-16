/* Tiny host test harness. */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>

static int test_failures, test_checks;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        test_checks++;                                                                             \
        if (!(cond)) {                                                                             \
            test_failures++;                                                                       \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                        \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        long long _a = (long long)(a), _b = (long long)(b);                                        \
        test_checks++;                                                                             \
        if (_a != _b) {                                                                            \
            test_failures++;                                                                       \
            printf("%s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b);          \
        }                                                                                          \
    } while (0)

static inline int test_report(const char *name)
{
    printf("%s: %d checks, %d failed\n", name, test_checks, test_failures);
    return test_failures != 0;
}

#endif
