cmake_minimum_required(VERSION 3.21)

project(AgeOfSkyrim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Age of Skyrim só compila no Windows (MSVC), assim como todo plugin SKSE.")
endif()

# Compilação paralela por arquivo (cl.exe /MP) pra TODO alvo configurado a partir daqui, inclusive o
# CommonLibSSE-NG vendorizado (sem precisar patchear o CMakeLists dele) — maior tempo de build é ele
# recompilando do zero a cada run do CI (não é um pacote vcpkg, o cache binário do vcpkg não cobre).
# /FS: evita contenção de escrita no .pdb compartilhado quando várias cl.exe do mesmo projeto rodam juntas.
if(MSVC)
    add_compile_options(/MP /FS)
endif()

# --- CommonLibSSE-NG (submódulo) --------------------------------------------
# Um único DLL para SE + AE. VR desligado; testes da lib desligados.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG EXCLUDE_FROM_ALL)
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

find_package(nlohmann_json CONFIG REQUIRED)

# --- Plugin -------------------------------------------------------------------
file(GLOB_RECURSE AOS_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h include/*.h)

add_commonlibsse_plugin(${PROJECT_NAME}
    AUTHOR "Leonardo"
    USE_ADDRESS_LIBRARY
    SOURCES ${AOS_SOURCES})

# include/ = API pública para outros plugins (AgeOfSkyrimAPI.h); o próprio mod usa o mesmo header.
target_include_directories(${PROJECT_NAME} PRIVATE src include)
target_compile_definitions(${PROJECT_NAME} PRIVATE AOS_VERSION="${PROJECT_VERSION}")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /Zc:preprocessor /W4 /wd4100)
endif()

# --- Deploy opcional ------------------------------------------------------------
# Defina a variável de ambiente AOS_DEPLOY_DIR para a pasta do mod no seu MO2/Vortex
# (ex.: D:\MO2\mods\AgeOfSkyrim). Após cada build o DLL e os dados são copiados.
if(DEFINED ENV{AOS_DEPLOY_DIR})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{AOS_DEPLOY_DIR}/SKSE/Plugins"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "$ENV{AOS_DEPLOY_DIR}/SKSE/Plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "$ENV{AOS_DEPLOY_DIR}")
endif()
