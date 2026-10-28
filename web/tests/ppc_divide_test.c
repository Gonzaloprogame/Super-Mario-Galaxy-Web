/* PowerPC's integer division on WebAssembly (tools/wasm_ppc_divide.py): divw/divwu never trap on the console, and the
   game goes on with the result (Nebula's sdk/os/divide_native.c gives the same results natively). Without the
   rewrite, the first division here is a WebAssembly trap ("integer divide by zero"). */
#include <stdint.h>
#include <stdio.h>

static int sFailures;

/* volatile: the compiler must not fold the divisions, so the module divides at run time. */
static volatile int32_t sZero = 0, sMinusOne = -1, sMin = INT32_MIN, sSeven = 7, sMinusSeven = -7;
static volatile uint32_t sUZero = 0, sUSeven = 7;
static volatile int64_t sZero64 = 0, sMinusSeven64 = -7, sMin64 = INT64_MIN, sMinusOne64 = -1;

/* Each result goes through memory before it is compared: with a division by zero being undefined in C, the compiler
   may otherwise answer the comparison without dividing (x / y == 0 becomes x < y for unsigned operands), which tests
   the compiler instead of the module's division. The rewrite covers the divisions the module executes, as Nebula's
   trap handler does natively; known sites that divide by zero keep __nebula_divw in their source. */
static volatile long long sResult;

static void expect(const char* what, long long got, long long want) {
    sResult = got;
    got = sResult;
    if (got != want) {
        printf("FAIL %s: %lld (want %lld)\n", what, got, want);
        sFailures++;
    }
}

int main(void) {
    expect("7 / 0", sSeven / sZero, 0);
    expect("-7 / 0", sMinusSeven / sZero, -1);
    expect("INT_MIN / -1", sMin / sMinusOne, -1);
    expect("7 % 0", sSeven % sZero, 7);
    expect("-7 % 0", sMinusSeven % sZero, -7);
    expect("INT_MIN % -1", sMin % sMinusOne, INT32_MAX); /* a - q * b with q = -1, wrapping */
    expect("7u / 0", sUSeven / sUZero, 0);
    expect("7u % 0", sUSeven % sUZero, 7);
    expect("-7 / 0 (64)", sMinusSeven64 / sZero64, -1);
    expect("-7 % 0 (64)", sMinusSeven64 % sZero64, -7);
    expect("INT64_MIN / -1", sMin64 / sMinusOne64, -1);
    /* Ordinary divisions are unchanged. */
    expect("-7 / 7", sMinusSeven / sSeven, -1);
    expect("7 % -7", sSeven % (int32_t)sMinusSeven, 0);
    expect("7u / 7", sUSeven / sUSeven, 1);
    printf(sFailures ? "%d failures\n" : "all passed\n", sFailures);
    return sFailures != 0;
}
