cmake_minimum_required(VERSION 3.20)

project(TeklaDB1 VERSION 0.3.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TEKLADB1_BUILD_TESTS "Build TeklaDB1 test and inspection tools" ON)
option(TEKLADB1_WITH_OCCT "Build the optional Open CASCADE geometry target" OFF)
set(TEKLADB1_OCCT_ROOT "" CACHE PATH "Optional Open CASCADE installation prefix")
set(TEKLADB1_OCCT_INCLUDE_DIR "" CACHE PATH
    "Existing Open CASCADE include directory; use with TEKLADB1_OCCT_LIBRARIES")
set(TEKLADB1_OCCT_LIBRARIES "" CACHE STRING
    "Existing Open CASCADE libraries; bypasses package discovery when supplied")

find_package(ZLIB REQUIRED)

add_library(tekla_db1
    src/Catalogs.cpp
    src/Parser.cpp
    src/Project.cpp
)
add_library(TeklaDB1::tekla_db1 ALIAS tekla_db1)
add_library(TeklaDB1::core ALIAS tekla_db1)
add_library(TeklaFormats::core ALIAS tekla_db1)
set_target_properties(tekla_db1 PROPERTIES
    EXPORT_NAME core
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(tekla_db1 PUBLIC cxx_std_17)
target_include_directories(tekla_db1
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(tekla_db1 PRIVATE ZLIB::ZLIB)

if(TEKLADB1_WITH_OCCT)
    if(TEKLADB1_OCCT_INCLUDE_DIR AND TEKLADB1_OCCT_LIBRARIES)
        set(_tekladb1_occt_include "${TEKLADB1_OCCT_INCLUDE_DIR}")
        set(_tekladb1_occt_libraries ${TEKLADB1_OCCT_LIBRARIES})
    else()
        if(TEKLADB1_OCCT_ROOT)
            find_package(OpenCASCADE REQUIRED COMPONENTS FoundationClasses ModelingData ModelingAlgorithms
                PATHS "${TEKLADB1_OCCT_ROOT}" NO_DEFAULT_PATH)
        else()
            find_package(OpenCASCADE REQUIRED COMPONENTS FoundationClasses ModelingData ModelingAlgorithms)
        endif()
        set(_tekladb1_occt_include "${OpenCASCADE_INCLUDE_DIR}")
        set(_tekladb1_occt_libraries
            TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
            TKPrim TKBO TKShHealing TKBool TKMesh)
    endif()
    add_library(tekla_db1_occt src/OcctGeometry.cpp)
    add_library(TeklaDB1::occt ALIAS tekla_db1_occt)
    add_library(TeklaFormats::occt ALIAS tekla_db1_occt)
    set_target_properties(tekla_db1_occt PROPERTIES
        EXPORT_NAME occt
        POSITION_INDEPENDENT_CODE ON)
    target_compile_features(tekla_db1_occt PUBLIC cxx_std_17)
    target_include_directories(tekla_db1_occt
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            ${_tekladb1_occt_include}
    )
    target_link_libraries(tekla_db1_occt PUBLIC tekla_db1 ${_tekladb1_occt_libraries})
    if(MSVC)
        target_compile_options(tekla_db1_occt PRIVATE /W4 /permissive-)
    else()
        target_compile_options(tekla_db1_occt PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(MSVC)
    target_compile_options(tekla_db1 PRIVATE /W4 /permissive-)
else()
    target_compile_options(tekla_db1 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TEKLADB1_BUILD_TESTS)
    enable_testing()
    add_executable(tekladb1_inspect tests/inspect.cpp)
    target_link_libraries(tekladb1_inspect PRIVATE TeklaDB1::tekla_db1)
    add_executable(tekladb1_catalog_inspect tests/catalog_inspect.cpp)
    target_link_libraries(tekladb1_catalog_inspect PRIVATE TeklaDB1::tekla_db1)
    add_executable(tekladb1_validate tests/validate.cpp)
    target_link_libraries(tekladb1_validate PRIVATE TeklaDB1::core)
    if(MINGW)
        target_link_options(tekladb1_inspect PRIVATE -municode)
        target_link_options(tekladb1_catalog_inspect PRIVATE -municode)
        target_link_options(tekladb1_validate PRIVATE -municode)
    endif()
    add_executable(tekladb1_regression tests/regression.cpp)
    target_link_libraries(tekladb1_regression PRIVATE TeklaDB1::core ZLIB::ZLIB)
    foreach(case valid_model short_trailer unknown_version wrong_fields broken_reference
            unhandled_part unverified_contour embedded_magic plain_limit gzip_limit
            gzip_concat gzip_corrupt gzip_truncated gzip_garbage truncations unicode_path
            ambiguity raw_retention project strict_companions catalog_error model_limit
            dbv_name project_precedence partial_project gzip_large duplicate_id nonfinite)
        add_test(NAME db1.${case} COMMAND tekladb1_regression "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    if(TEKLADB1_WITH_OCCT)
        add_executable(tekladb1_geometry_test tests/geometry.cpp)
        target_link_libraries(tekladb1_geometry_test PRIVATE TeklaDB1::occt)
        add_test(NAME db1.geometry_volumes COMMAND tekladb1_geometry_test)
    endif()
endif()

install(TARGETS tekla_db1
    EXPORT TeklaDB1Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(TEKLADB1_WITH_OCCT)
    install(TARGETS tekla_db1_occt
        EXPORT TeklaDB1Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT TeklaDB1Targets
    FILE TeklaDB1Targets.cmake
    NAMESPACE TeklaDB1::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TeklaDB1
)

configure_package_config_file(
    cmake/TeklaDB1Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TeklaDB1Config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TeklaDB1
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/TeklaDB1ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TeklaDB1Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TeklaDB1ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TeklaDB1
)

configure_package_config_file(cmake/TeklaFormatsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TeklaFormatsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TeklaFormats)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/TeklaFormatsConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/TeklaFormatsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TeklaFormatsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TeklaFormats)
