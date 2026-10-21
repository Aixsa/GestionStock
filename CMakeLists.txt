cmake_minimum_required(VERSION 3.22)
project(GestionStock VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

add_executable(gestion-stock
    src/main.cpp
    src/app.cpp
    src/database.cpp
    src/security.cpp
    src/reports.cpp
    src/barcode.cpp
    src/i18n.cpp
    src/preferences.cpp
)
target_include_directories(gestion-stock PRIVATE src ${GTK4_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS})
target_link_directories(gestion-stock PRIVATE ${GTK4_LIBRARY_DIRS} ${SQLITE3_LIBRARY_DIRS})
target_compile_options(gestion-stock PRIVATE ${GTK4_CFLAGS_OTHER} ${SQLITE3_CFLAGS_OTHER})
target_link_libraries(gestion-stock PRIVATE ${GTK4_LIBRARIES} ${SQLITE3_LIBRARIES})
target_compile_definitions(gestion-stock PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    APP_NAME="GestionStock"
)
if(WIN32)
    target_link_libraries(gestion-stock PRIVATE shell32)
endif()
if(MSVC)
    target_compile_options(gestion-stock PRIVATE /W4 /permissive-)
else()
    target_compile_options(gestion-stock PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS gestion-stock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/gestion-stock.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

include(CTest)
if(BUILD_TESTING)
    add_executable(gestion-stock-tests
        tests/database_tests.cpp
        src/database.cpp
        src/security.cpp
        src/reports.cpp
        src/barcode.cpp
        src/i18n.cpp
        src/preferences.cpp
    )
    target_include_directories(gestion-stock-tests PRIVATE src ${GTK4_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS})
    target_link_directories(gestion-stock-tests PRIVATE ${GTK4_LIBRARY_DIRS} ${SQLITE3_LIBRARY_DIRS})
    target_compile_options(gestion-stock-tests PRIVATE ${GTK4_CFLAGS_OTHER} ${SQLITE3_CFLAGS_OTHER})
    target_link_libraries(gestion-stock-tests PRIVATE ${GTK4_LIBRARIES} ${SQLITE3_LIBRARIES})
    if(WIN32)
        target_link_libraries(gestion-stock-tests PRIVATE shell32)
    endif()
    if(MSVC)
        target_compile_options(gestion-stock-tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(gestion-stock-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gestion-stock-integration COMMAND gestion-stock-tests)
endif()
