# Warning flags for ztodo's own targets (not third_party).
#   -DZTODO_WERROR=ON   treat warnings as errors (CI sets this; off by
#                           default so a newer compiler doesn't break a build)
option(ZTODO_WERROR "Treat compiler warnings in ztodo's own targets as errors" OFF)

function(ztodo_set_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-align -Wunused -Woverloaded-virtual -Wnull-dereference)
  if(ZTODO_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

