#!/usr/bin/env bash
#
# mayhem/build.sh — build monkey's fuzz harnesses (sanitized), their standalone reproducers, and the
# test suite + known-answer oracle (normal flags). Offline: the tree bundles all of its dependencies.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Library-only configuration: the core HTTP parser, config reader and string/URL helpers, no TLS.
CMAKE_COMMON=(-DMK_LIB_ONLY=On -DMK_TLS=Off -DMK_WITHOUT_BIN=On
              -DMK_WITHOUT_CONF=On -DMK_PLUGIN_DIRLISTING=No -DMK_PLUGIN_MANDRIL=No)

# 1) The project itself, sanitized + coverage-instrumented (fuzzer-no-link), DWARF 3 (build/ is gitignored upstream).
cmake -B build/fuzz "${CMAKE_COMMON[@]}" -DMK_TESTS=Off \
      -DCMAKE_C_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
cmake --build build/fuzz -j"$MAYHEM_JOBS" --target monkey-core-static

FUZZ_INC=(-I"$SRC/include" -I"$SRC/include/monkey" -I"$SRC/build/fuzz/include" -I"$SRC/build/fuzz/include/monkey")
FUZZ_LIBS=("$SRC/build/fuzz/library/libmonkey.a" "$SRC/build/fuzz/library/libmk_core.a"
           "$SRC/build/fuzz/library/librbtree.a" "$SRC/build/fuzz/library/libco.a"
           "$SRC/build/fuzz/library/libregex.a" -lpthread -ldl -lm)

# LeakSanitizer is off for every target: leaks are not the bug class fuzzed here.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o /tmp/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o

# 2) Each harness twice: libFuzzer binary + standalone run-once reproducer.
for h in fuzz_http_parser fuzz_rconf fuzz_utils; do
    $CC -O1 $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "${FUZZ_INC[@]}" \
        "$SRC/mayhem/$h.c" /tmp/lsan_off.o "${FUZZ_LIBS[@]}" -o "/mayhem/$h"
    $CC -O1 $SANITIZER_FLAGS $DEBUG_FLAGS "${FUZZ_INC[@]}" \
        "$SRC/mayhem/$h.c" /tmp/standalone_main.o /tmp/lsan_off.o "${FUZZ_LIBS[@]}" \
        -o "/mayhem/$h-standalone"
done

# 3) Upstream unit tests + the known-answer oracle, normal flags, independent build tree.
cmake -B build/tests "${CMAKE_COMMON[@]}" -DMK_TESTS=On -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="$COVERAGE_FLAGS"
cmake --build build/tests -j"$MAYHEM_JOBS"
$CC -O2 $COVERAGE_FLAGS -I"$SRC/include" -I"$SRC/include/monkey" -I"$SRC/build/tests/include" -I"$SRC/build/tests/include/monkey" \
    "$SRC/mayhem/oracle_test.c" \
    "$SRC/build/tests/library/libmonkey.a" "$SRC/build/tests/library/libmk_core.a" \
    "$SRC/build/tests/library/librbtree.a" "$SRC/build/tests/library/libco.a" \
    "$SRC/build/tests/library/libregex.a" -lpthread -ldl -lm \
    -o "$SRC/build/tests/bin/monkey_oracle"
