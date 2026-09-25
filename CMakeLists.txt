cmake_minimum_required(VERSION 3.21)

project(Fetchora VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# QWidget front end. Qt Quick is deliberately NOT linked: the UI is built from
# .ui files plus C++, so the QML runtime and its dependencies are no longer
# part of the application.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Sql Svg Concurrent LinguistTools)

qt_standard_project_setup(REQUIRES 6.5)

# ------------------------------------------------------- Windows app icon
# Explorer, the taskbar and Alt+Tab need a real icon resource; version.rc also
# carries the file properties shown in Explorer's Details tab.
set(APP_RESOURCE_SOURCES "")
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in")
    enable_language(RC)
    # version.rc.in is UTF-8: tell windres so the Chinese strings are not
    # decoded as the system ANSI codepage (which turns them into mojibake).
    if(MINGW)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")
    endif()

    # Every variable used by version.rc.in must exist *before* configure_file()
    # runs - it substitutes at call time.
    set(APP_COMMA_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(APP_PRODUCT_NAME "Fetchora")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        @ONLY)
    set(APP_RESOURCE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()

# ------------------------------------------------------------------ backend
# Framework agnostic: plain QObject/JSON-RPC/process code shared by any UI.
set(APP_BACKEND_SOURCES
    Aria2Client.cpp       Aria2Client.h
    Aria2Process.cpp      Aria2Process.h
    Aria2Manager.cpp      Aria2Manager.h
    SettingsManager.cpp   SettingsManager.h
    DownloadHistory.cpp   DownloadHistory.h
    TorrentUtils.cpp      TorrentUtils.h
    HttpServer.cpp        HttpServer.h
    ClipboardHelper.cpp   ClipboardHelper.h
    UpdateChecker.cpp     UpdateChecker.h
    NotificationManager.cpp NotificationManager.h
)

# --------------------------------------------------------------- ui framework
# The Fluent/WinUI 3 widget set: design tokens, chrome and self-painted controls.
file(GLOB APP_UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.h"
)

# --------------------------------------------------------------------- pages
# One .ui + .h + .cpp triple per screen. GLOBbed (with CONFIGURE_DEPENDS) so a
# new page only has to be dropped into ui/pages/ to be picked up; AUTOUIC then
# resolves the matching #include "ui_<Page>.h".
file(GLOB APP_PAGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/pages/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/pages/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/pages/*.ui"
)

qt_add_executable(Fetchora
    main.cpp
    ${APP_BACKEND_SOURCES}
    ${APP_UI_SOURCES}
    ${APP_PAGE_SOURCES}
    ${APP_RESOURCE_SOURCES}
)

target_include_directories(Fetchora PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# AUTOUIC resolves #include "ui_X.h" against the .ui file's own directory.
set_source_files_properties(${APP_PAGE_SOURCES} PROPERTIES SKIP_AUTOUIC FALSE)

# ------------------------------------------------------------------- identity
# One name everywhere: Fetchora on Unix, Fetchora.exe on Windows, Fetchora.app
# (whose CFBundleExecutable is Fetchora) on macOS. Setting OUTPUT_NAME explicitly
# keeps that true even if the CMake target is ever renamed.
set_target_properties(Fetchora PROPERTIES
    OUTPUT_NAME Fetchora
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Fetchora"
    # CFBundleIdentifier is a reverse-DNS string and is REQUIRED by macOS for a
    # bundle to behave (notarisation, Launch Services registration, preferences).
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.fetchora.downloadmanager"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    # Finder shows the short version; keeping the patch level out of it made the
    # bundle claim "0.1" while the app called itself 0.1.4.
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE
)

# The macOS icon is optional: CMake cannot invent a genuine .icns. When one is
# present in the source tree (see tools/make-icns.js, which rebuilds it from the
# PNGs make-icons.ps1 already generates) it is copied into the bundle and named
# in Info.plist; otherwise CFBundleIconFile stays empty and Finder falls back to
# the generic application icon. main.cpp always draws the real logo at runtime,
# so the in-app icon is correct either way.
if(APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.icns")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/app.icns"
        "${CMAKE_CURRENT_BINARY_DIR}/app.icns"
        COPYONLY)
    set_target_properties(Fetchora PROPERTIES MACOSX_BUNDLE_ICON_FILE "app.icns")
    # `cmake -E copy_if_different` does NOT create missing directories, and CMake
    # only creates Contents/Resources when a source file declares
    # MACOSX_PACKAGE_LOCATION - nothing here does. Without the make_directory the
    # POST_BUILD copy fails with "Error copying file ... No such file or
    # directory" and the whole build stops, on macOS only.
    add_custom_command(TARGET Fetchora POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:Fetchora>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_BINARY_DIR}/app.icns"
                "$<TARGET_BUNDLE_CONTENT_DIR:Fetchora>/Resources/app.icns"
        COMMENT "Copying app.icns into the bundle")
endif()

target_link_libraries(Fetchora
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Sql
        Qt6::Svg
        Qt6::Concurrent
)

# ------------------------------------------------------------- translations
# Source strings are Simplified Chinese; translations/*.ts carries the English
# (and any future) translation. Missing .ts files are skipped so a fresh clone
# still configures.
set(APP_TS_FILES "")
foreach(lang IN ITEMS en zh_CN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/translations/fetchora_${lang}.ts")
        list(APPEND APP_TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/fetchora_${lang}.ts")
    endif()
endforeach()
if(APP_TS_FILES)
    qt_add_translations(Fetchora
        TS_FILES ${APP_TS_FILES}
        RESOURCE_PREFIX "/i18n"
        LUPDATE_OPTIONS -no-obsolete -locations none
    )
endif()

# ---------------------------------------------------------------- Windows bits
if(WIN32)
    target_link_libraries(Fetchora PRIVATE dwmapi)

    # Fold the GCC runtime into the executable: without this the app only runs
    # when the MinGW bin directory happens to be on PATH, and a distributed
    # build fails with STATUS_DLL_NOT_FOUND (0xC0000135).
    if(MINGW)
        target_link_options(Fetchora PRIVATE
            -static-libgcc
            -static-libstdc++
            -static
        )
    endif()
endif()

# --------------------------------------------------------- engine + build name
# aria2 is an external program on every platform; only its name differs. Windows
# ships/installs aria2c.exe, Unix installs aria2c.
if(WIN32)
    set(APP_ENGINE_NAME "aria2c.exe")
else()
    set(APP_ENGINE_NAME "aria2c")
endif()

# On Windows and Linux the runtime files sit next to the binary. Inside a macOS
# bundle they go into Contents/Resources, because Contents/MacOS is reserved for
# executables and would confuse codesign's bundle validation.
if(APPLE)
    set(APP_RUNTIME_DIR "$<TARGET_BUNDLE_CONTENT_DIR:Fetchora>/Resources")
else()
    set(APP_RUNTIME_DIR "$<TARGET_FILE_DIR:Fetchora>")
endif()

foreach(extra IN ITEMS ${APP_ENGINE_NAME} ca-bundle.crt)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${extra}")
        add_custom_command(TARGET Fetchora POST_BUILD
            # Same reason as the icon above: inside a bundle Contents/Resources
            # is not guaranteed to exist yet.
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RUNTIME_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${extra}"
                    "${APP_RUNTIME_DIR}/${extra}"
            COMMENT "Copying ${extra}")
    endif()
endforeach()

# ------------------------------------------------------------------ Unix bits
# GNUInstallDirs has to be included before any rule that references
# CMAKE_INSTALL_DATADIR or CMAKE_INSTALL_BINDIR, so it comes first here.
include(GNUInstallDirs)

if(UNIX AND NOT APPLE)
    # Deliberately NO -static here. A distribution Qt (qt6-base-dev, Homebrew,
    # ...) is built as a shared library, so -static has nothing to link against
    # and the link fails with "cannot find -lQt6Widgets". Shipping a portable
    # Linux binary means bundling the shared Qt libraries (linuxdeploy,
    # AppImage, ...) or building Qt itself statically; neither belongs in this
    # CMakeLists. -static-libgcc / -static-libstdc++ are equally pointless on
    # Linux, where the compiler runtime is a distro package that is always
    # present.

    # Desktop integration: fetchora.desktop + the 256 px icon, so the app appears
    # in the applications menu with the right name and picture after
    # `cmake --install`. The icon goes into the hicolor theme, which is where the
    # desktop entry's `Icon=fetchora` looks for it.
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/fetchora.desktop.in"
        "${CMAKE_CURRENT_BINARY_DIR}/fetchora.desktop"
        @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fetchora.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/app-256.png"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME fetchora.png)
endif()

install(TARGETS Fetchora
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
