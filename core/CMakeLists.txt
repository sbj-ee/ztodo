# ztodo_core: no widgets. Storage (Box folder op logs), views, quick-add
# parsing, recurrence, settings, and the signed auto-update logic.
add_library(ztodo_core STATIC
  ConfigPaths.hpp Sodium.hpp
  Store.cpp Store.hpp
  UndoStack.cpp UndoStack.hpp
  Views.cpp Views.hpp
  DateText.cpp DateText.hpp
  Recurrence.cpp Recurrence.hpp
  Reminders.cpp Reminders.hpp
  QuickAdd.cpp QuickAdd.hpp
  Settings.cpp Settings.hpp
  Update.cpp Update.hpp
  UpdateSigningKey.cpp
  Minisign.cpp Minisign.hpp
  UpdateChecker.cpp UpdateChecker.hpp
)
target_include_directories(ztodo_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${ZTODO_GENERATED_DIR})
target_link_libraries(ztodo_core PUBLIC Qt6::Core Qt6::Network PkgConfig::SODIUM)
ztodo_set_warnings(ztodo_core)

# Development-only environment overrides ($ZTODO_UPDATE_URL); never in releases.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_zt_dev_overrides_default ON)
else()
  set(_zt_dev_overrides_default OFF)
endif()
option(ZTODO_DEV_OVERRIDES "Honour development environment overrides (debug builds only)" ${_zt_dev_overrides_default})
if(ZTODO_DEV_OVERRIDES)
  target_compile_definitions(ztodo_core PUBLIC ZTODO_DEV_OVERRIDES)
endif()
