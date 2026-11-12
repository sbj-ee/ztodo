# Install rules + CPack: .deb on Linux amd64, .dmg (DragNDrop) on Apple Silicon.
# Version comes solely from project(ztodo VERSION ...) -> PROJECT_VERSION.
# Artifact names (the in-app updater expects exactly these):
#   ztodo_<ver>_amd64.deb
#   ztodo-<ver>-Darwin.dmg

include(GNUInstallDirs)

set(CPACK_PACKAGE_NAME "ztodo")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Stephen B. Johnson")
set(CPACK_PACKAGE_CONTACT "Stephen B. Johnson <49662809+sbj-ee@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/sbj-ee/ztodo")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Todoist-style task manager for Linux and macOS that syncs through Box (Qt 6)")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(UNIX AND NOT APPLE)
  install(TARGETS ztodo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/ztodo.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/ztodo RENAME copyright)
  install(FILES ${CMAKE_SOURCE_DIR}/assets/ztodo.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  foreach(_sz 16 24 32 48 64 128 256 512)
    install(FILES ${CMAKE_SOURCE_DIR}/assets/icons/ztodo-${_sz}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_sz}x${_sz}/apps
            RENAME ztodo.png)
  endforeach()

  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-wayland, libsodium23")
  # Help > Check for Updates installs through `pkexec apt install`; rclone
  # is how ~/Box is synced on Linux (see README).
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pkexec | policykit-1")
  set(CPACK_DEBIAN_PACKAGE_SUGGESTS "rclone")
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "ztodo ${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "ztodo-${PROJECT_VERSION}-Darwin")
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
  install(TARGETS ztodo BUNDLE DESTINATION .)
  if(DEFINED ZTODO_ICNS_FILE)
    install(FILES "${ZTODO_ICNS_FILE}" DESTINATION ztodo.app/Contents/Resources)
  endif()
  install(SCRIPT "${CMAKE_BINARY_DIR}/MacDeploy.cmake")
else()
  message(FATAL_ERROR "ztodo: packaging is only configured for Linux amd64 and macOS arm64")
endif()

include(CPack)
