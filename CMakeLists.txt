cmake_minimum_required(VERSION 3.16)
project(duckclinvarbitration LANGUAGES C)

set(EXTENSION_NAME duckclinvarbitration CACHE STRING "DuckDB extension name")
set(DUCKDB_EXTENSION_API_VERSION_UNSTABLE "v1.5.2" CACHE STRING "Pinned DuckDB C ABI")

# The C entrypoint uses the exact DuckDB headers corresponding to the binary.
set(CAPI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/duckdb_capi/${DUCKDB_EXTENSION_API_VERSION_UNSTABLE}")
if(NOT EXISTS "${CAPI_DIR}/duckdb_extension.h")
    message(FATAL_ERROR "No pinned C API headers for ${DUCKDB_EXTENSION_API_VERSION_UNSTABLE}")
endif()

# Refuse dynamic libxml2/zlib: a loadable extension must not depend on the
# host's XML parser ABI or publish its symbols into the DuckDB process.
if(NOT WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()
find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)
foreach(archive IN ITEMS LIBXML2_LIBRARY ZLIB_LIBRARY)
    if(NOT WIN32 AND NOT "${${archive}}" MATCHES "\\.a$")
        message(FATAL_ERROR "${archive} must be a static archive: ${${archive}}")
    endif()
endforeach()

if(DUCKDB_WASM_EXTENSION)
    add_library(${EXTENSION_NAME} STATIC src/duckclinvarbitration_extension.c)
else()
    add_library(${EXTENSION_NAME} SHARED src/duckclinvarbitration_extension.c)
endif()
target_compile_features(${EXTENSION_NAME} PRIVATE c_std_11)
target_compile_definitions(${EXTENSION_NAME} PRIVATE
    DUCKDB_EXTENSION_NAME=duckclinvarbitration
    DUCKDB_EXTENSION_API_VERSION_UNSTABLE=${DUCKDB_EXTENSION_API_VERSION_UNSTABLE})
target_include_directories(${EXTENSION_NAME} PRIVATE "${CAPI_DIR}")
target_link_libraries(${EXTENSION_NAME} PRIVATE LibXml2::LibXml2 ZLIB::ZLIB)
set_target_properties(${EXTENSION_NAME} PROPERTIES C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(UNIX AND NOT APPLE AND NOT DUCKDB_WASM_EXTENSION)
    target_link_options(${EXTENSION_NAME} PRIVATE "LINKER:--exclude-libs,ALL")
endif()
if(APPLE)
    target_link_options(${EXTENSION_NAME} PRIVATE "LINKER:-exported_symbol,_duckclinvarbitration_init_c_api")
endif()

if(DUCKDB_WASM_EXTENSION)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/wasm_fat_archive_inputs.txt"
        CONTENT "$<TARGET_FILE:LibXml2::LibXml2>\n$<TARGET_FILE:ZLIB::ZLIB>\n")
    add_custom_command(TARGET ${EXTENSION_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DARCHIVE=$<TARGET_FILE:${EXTENSION_NAME}>
            -DINPUT_LIST=${CMAKE_BINARY_DIR}/wasm_fat_archive_inputs.txt
            -DARCHIVER=${CMAKE_AR} -DRANLIB=${CMAKE_RANLIB}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/merge_static_archives.cmake
        VERBATIM)
endif()
