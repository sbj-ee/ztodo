# ztodo_ui: widgets shared by the app and the GUI tests.
add_library(ztodo_ui STATIC
  Theme.hpp
  Appearance.cpp Appearance.hpp
  AppFont.cpp AppFont.hpp
  Delegates.cpp Delegates.hpp
  MainWindow.cpp MainWindowNav.cpp MainWindow.hpp
  TaskDialog.cpp TaskDialog.hpp
  SettingsDialog.cpp SettingsDialog.hpp
  UpdateDialog.cpp UpdateDialog.hpp
  UpdateManager.cpp UpdateManager.hpp
)
target_include_directories(ztodo_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ztodo_ui PUBLIC ztodo_core Qt6::Gui Qt6::Widgets)
ztodo_set_warnings(ztodo_ui)

add_executable(ztodo main.cpp resources.qrc)
target_link_libraries(ztodo PRIVATE ztodo_ui)
ztodo_set_warnings(ztodo)

# macOS: copy the generated icns into the build-tree .app (POST_BUILD must
# live in the directory that created the target).
if(APPLE)
  add_custom_command(TARGET ztodo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ztodo>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/ztodo.icns"
            "$<TARGET_BUNDLE_CONTENT_DIR:ztodo>/Resources/ztodo.icns"
    COMMENT "Copying ztodo.icns into ztodo.app"
    VERBATIM)
endif()
