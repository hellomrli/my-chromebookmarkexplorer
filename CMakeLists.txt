cmake_minimum_required(VERSION 3.21)

project(ChromeBookmarkExplorer VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(ChromeBookmarkExplorer WIN32
    src/main.cpp
    src/BookmarkNode.h
    src/BookmarkNode.cpp
    src/BookmarkDocument.h
    src/BookmarkDocument.cpp
    src/ChromeProfiles.h
    src/ChromeProfiles.cpp
    src/HealthChecker.h
    src/HealthChecker.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

target_link_libraries(ChromeBookmarkExplorer PRIVATE Qt6::Widgets Qt6::Network)

if (MSVC)
    target_compile_options(ChromeBookmarkExplorer PRIVATE /W4)
else()
    target_compile_options(ChromeBookmarkExplorer PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS ChromeBookmarkExplorer
    RUNTIME DESTINATION .
)

set(CPACK_PACKAGE_NAME "ChromeBookmarkExplorer")
set(CPACK_PACKAGE_VENDOR "hellomrli")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
include(CPack)
