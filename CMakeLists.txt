cmake_minimum_required(VERSION 3.16)
project(keylogger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Этот проект предназначен только для Windows")
endif()

add_definitions(-D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE)

add_executable(keylogger WIN32 keylogger.cpp)  # WIN32 = без консоли

target_link_libraries(keylogger PRIVATE
    user32
    shell32
    winmm
)

# Для MSVC
if(MSVC)
    target_compile_options(keylogger PRIVATE /W4 /WX /EHsc)
    target_compile_definitions(keylogger PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(keylogger PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

# Для MinGW
if(MINGW)
    target_compile_options(keylogger PRIVATE -Wall -Wextra -pedantic)
    target_link_options(keylogger PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(keylogger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Полезные кастомные цели
add_custom_target(run
    COMMAND keylogger
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Запуск keylogger"
)

add_custom_target(stop
    COMMAND keylogger --stop
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Остановка keylogger"
)