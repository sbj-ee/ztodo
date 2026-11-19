#pragma once

// One harness, two builds:
//  - ZTODO_LIBFUZZER (clang, -DZTODO_FUZZ=ON): libFuzzer provides main() and
//    calls these entry points; run the binary to fuzz.
//  - otherwise: FuzzMain.cpp provides a main() that feeds each file (or each
//    file in each directory) given on the command line through the harness
//    once. ctest runs it over fuzz/corpus/<target>, so every seed and every
//    crash ever found stays a regression test on every compiler.

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#define ZTODO_FUZZ_INIT extern "C" int LLVMFuzzerInitialize(int *, char ***)
#define ZTODO_FUZZ_TARGET extern "C" int LLVMFuzzerTestOneInput
