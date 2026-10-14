cmake_minimum_required(VERSION 3.21)
project(LogScope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
include(CTest)

add_library(logscope_core STATIC
    src/core/logindex.h src/core/logindex.cpp
    src/core/jobs.h src/core/jobs.cpp
)
target_include_directories(logscope_core PUBLIC src)
target_link_libraries(logscope_core PUBLIC Qt6::Core)

add_library(logscope_ui STATIC
    src/ui/logmodel.h src/ui/logmodel.cpp
    src/ui/logtab.h src/ui/logtab.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
)
target_link_libraries(logscope_ui PUBLIC logscope_core Qt6::Gui Qt6::Widgets)

qt_add_executable(LogScope WIN32 src/main.cpp resources/resources.qrc)
target_link_libraries(LogScope PRIVATE logscope_ui)
set_target_properties(LogScope PROPERTIES OUTPUT_NAME LogScope)
install(TARGETS LogScope RUNTIME DESTINATION .)

add_executable(LogScopeBench tools/benchmark.cpp)
target_link_libraries(LogScopeBench PRIVATE logscope_core)
if(WIN32)
    target_link_libraries(LogScopeBench PRIVATE psapi)
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(LogScopeTests tests/test_logscope.cpp resources/resources.qrc)
    set_target_properties(LogScopeTests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(LogScopeTests PRIVATE logscope_ui Qt6::Test)
    add_test(NAME LogScopeTests COMMAND LogScopeTests)
    set_tests_properties(LogScopeTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(LogScopeTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
endif()

foreach(target logscope_core logscope_ui LogScope LogScopeBench)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
if(MSVC AND BUILD_TESTING)
    target_compile_options(LogScopeTests PRIVATE /W4 /utf-8 /permissive-)
endif()
