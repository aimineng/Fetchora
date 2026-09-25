cmake_minimum_required(VERSION 3.21)
project(screencap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Development-only helper. Not part of the shipped application.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

qt_add_executable(screencap main.cpp)
target_link_libraries(screencap PRIVATE Qt6::Widgets)

# Keep a console so the tool can report what it wrote.
set_target_properties(screencap PROPERTIES WIN32_EXECUTABLE FALSE)

# Fold the GCC runtime in, otherwise the tool only starts when the MinGW bin
# directory happens to be on PATH (STATUS_DLL_NOT_FOUND otherwise).
if(MINGW)
    target_link_options(screencap PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
