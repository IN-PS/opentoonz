// Old LZO 2.03 compile-time asserts use non-constant expressions that
// modern compilers reject; disable them (checks are informational only).
#define ACC_COMPILE_TIME_ASSERT_HEADER(e)
