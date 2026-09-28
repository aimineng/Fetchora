# The unit tests: what can be checked without an engine, a window or a network.
#
# `--self-test` runs the same checks inside the app, which is fine for a bug report
# and for CI on a machine that has the whole thing built - but it cannot be run while
# a file is being edited, it cannot be filtered, and a failure only says which line
# printed it. These are the same parsers behind a real test runner, so `ctest` is the
# first thing to reach for when touching them.
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(FetchoraTests
    tst_torrentutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TorrentUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TrackerSources.cpp
)

target_include_directories(FetchoraTests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core)

target_link_libraries(FetchoraTests PRIVATE Qt6::Core Qt6::Test)

# AUTOMOC has to see the test's own files; the app's are already mocced in its target.
set_target_properties(FetchoraTests PROPERTIES AUTOMOC ON)

add_test(NAME torrentutils COMMAND FetchoraTests)
