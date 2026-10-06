cmake_minimum_required(VERSION 3.21)

project(ChromeBookmarkExplorer VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

include(CTest)

set(EXPLORER_SOURCES
    src/BookmarkDragDrop.h
    src/BookmarkDragDrop.cpp
    src/BookmarkIconView.h
    src/BookmarkIconView.cpp
    src/BookmarkNode.h
    src/BookmarkNode.cpp
    src/BookmarkDocument.h
    src/BookmarkDocument.cpp
    src/ChromeProfiles.h
    src/ChromeProfiles.cpp
    src/HealthChecker.h
    src/HealthChecker.cpp
    src/Updater.h
    src/Updater.cpp
    src/BatchEditDialog.h
    src/BatchEditDialog.cpp
    src/ImportExport.h
    src/ImportExport.cpp
    src/Logger.h
    src/Logger.cpp
    src/TagDialog.h
    src/TagDialog.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    resources/icons.qrc
)

qt_add_executable(ChromeBookmarkExplorer WIN32 src/main.cpp ${EXPLORER_SOURCES})

if (WIN32)
    target_sources(ChromeBookmarkExplorer PRIVATE resources/app.rc)
endif()

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

if (BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(HealthCheckerTests
        tests/HealthCheckerTest.cpp
        src/BookmarkNode.h
        src/BookmarkNode.cpp
        src/HealthChecker.h
        src/HealthChecker.cpp
    )
    target_include_directories(HealthCheckerTests PRIVATE src)
    target_link_libraries(HealthCheckerTests PRIVATE Qt6::Network Qt6::Test)

    if (MSVC)
        target_compile_options(HealthCheckerTests PRIVATE /W4)
    else()
        target_compile_options(HealthCheckerTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME HealthCheckerTests COMMAND HealthCheckerTests)

    qt_add_executable(BookmarkDocumentTests
        tests/BookmarkDocumentTest.cpp
        src/BookmarkDocument.cpp src/BookmarkNode.cpp src/ChromeProfiles.cpp)
    target_include_directories(BookmarkDocumentTests PRIVATE src)
    target_link_libraries(BookmarkDocumentTests PRIVATE Qt6::Test)
    add_test(NAME BookmarkDocumentTests COMMAND BookmarkDocumentTests)

    qt_add_executable(BookmarkExplorerTests tests/BookmarkExplorerTest.cpp ${EXPLORER_SOURCES})
    target_include_directories(BookmarkExplorerTests PRIVATE src)
    target_link_libraries(BookmarkExplorerTests PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
    add_test(NAME BookmarkExplorerTests COMMAND BookmarkExplorerTests)
    set_target_properties(HealthCheckerTests BookmarkDocumentTests BookmarkExplorerTests
        PROPERTIES WIN32_EXECUTABLE FALSE)
    set_tests_properties(HealthCheckerTests BookmarkDocumentTests BookmarkExplorerTests
        PROPERTIES TIMEOUT 120)
    set_tests_properties(BookmarkExplorerTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
