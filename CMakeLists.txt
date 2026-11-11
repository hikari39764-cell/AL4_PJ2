cmake_minimum_required(VERSION 3.28)

project(Project_AL4_PJ2 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "HIKARI GameProjects require x64 Visual Studio 2022.")
endif()

find_package(HIKARI CONFIG REQUIRED)

hikari_add_game_project(
    NAME "Project_AL4_PJ2"
    SOURCE_DIR Source/Runtime)
