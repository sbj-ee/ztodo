# Fuzz harnesses (see FuzzMain.hpp). Always built with the tests as corpus
# replayers that ctest runs over fuzz/corpus/<target>; with -DZTODO_FUZZ=ON
# (clang only) they are libFuzzer binaries instead:
#
#   cmake -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#         -DZTODO_FUZZ=ON -DZTODO_SANITIZE=ON
#   cmake --build build-fuzz
#   mkdir -p /tmp/corpus && build-fuzz/fuzz/fuzz_oplog /tmp/corpus fuzz/corpus/fuzz_oplog -max_total_time=60
#
# A crash leaves crash-<hash> in the working directory: fix the bug, then add
# that file to fuzz/corpus/<target>/ so it stays a regression test.

function(ztodo_fuzz name)
  if(ZTODO_FUZZ)
    add_executable(${name} ${name}.cpp FuzzMain.hpp)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  else()
    add_executable(${name} ${name}.cpp FuzzMain.hpp FuzzMain.cpp)
  endif()
  target_link_libraries(${name} PRIVATE ztodo_core)
  ztodo_set_warnings(${name})
  if(ZTODO_FUZZ)
    # Given files (not a directory), libFuzzer runs each once and exits.
    file(GLOB _seeds CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}/*)
    add_test(NAME ${name}_corpus COMMAND ${name} ${_seeds})
  else()
    add_test(NAME ${name}_corpus COMMAND ${name} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
  endif()
endfunction()

ztodo_fuzz(fuzz_oplog)
ztodo_fuzz(fuzz_text)
ztodo_fuzz(fuzz_update)
