
function(add_install_rules)

  # For now installation is only supported on Linux
  if (DOSBOX_PLATFORM_LINUX)

    set(INSTALL_DIR_DOC       "${CMAKE_INSTALL_DOCDIR}")
    set(INSTALL_DIR_MAN       "${CMAKE_INSTALL_MANDIR}")
    set(INSTALL_DIR_RESOURCES "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
    set(INSTALL_DIR_DESKTOP   "${CMAKE_INSTALL_DATADIR}/applications")
    set(INSTALL_DIR_ICONS     "${CMAKE_INSTALL_DATADIR}/icons/hicolor")
    set(INSTALL_DIR_METAINFO  "${CMAKE_INSTALL_DATADIR}/metainfo")

    set(INSTALL_ICON_NAME "org.dosbox_automation.dosbox_automation")

    # Install the application binary
    install(TARGETS dosbox RUNTIME)

    # System manual page
    install(FILES "docs/dosbox.1"
      DESTINATION "${INSTALL_DIR_MAN}/man1")

    # Application menu entry
    install(FILES "extras/linux/${INSTALL_ICON_NAME}.desktop"
      DESTINATION "${INSTALL_DIR_DESKTOP}")

    # Software component information
    install(FILES "extras/linux/${INSTALL_ICON_NAME}.metainfo.xml"
      DESTINATION "${INSTALL_DIR_METAINFO}")

    # Scalable icon
    install(FILES "resources/icons/svg/dosbox-automation.svg"
      DESTINATION "${INSTALL_DIR_ICONS}/scalable/apps"
      RENAME "${INSTALL_ICON_NAME}.svg")

    # Bitmap icons
    foreach(PX IN ITEMS 16 22 24 32 48 96 128 256 512 1024)
    install(FILES "resources/icons/png/icon_${PX}.png"
        DESTINATION "${INSTALL_DIR_ICONS}/${PX}x${PX}/apps"
        RENAME "${INSTALL_ICON_NAME}.png")
    endforeach()

    # Bundle the resources (trailing slash = copy contents, not the
    # directory itself, so shaders/ lands at share/<project>/shaders/
    # rather than share/<project>/resources/shaders/).
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${RESOURCE_COPY_PATH}/"
            DESTINATION "${INSTALL_DIR_RESOURCES}")

    # Bundle required licenses. Upstream collected per-dependency
    # licenses into doc/licenses; this fork ships one combined
    # THIRD_PARTY_LICENSES.txt instead.
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/doc/LICENSE"
            "${CMAKE_CURRENT_BINARY_DIR}/doc/THIRD_PARTY_LICENSES.txt"
            DESTINATION "${INSTALL_DIR_DOC}")

  endif()

endfunction()
