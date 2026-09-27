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
    src/ShapeXml.cpp
    src/MetadataXml.cpp
    src/DrawingMetadata.cpp
    src/AnalysisSettings.cpp
    src/GuidMappings.cpp
    src/TinyXmlPrivate.cpp
    src/Parser.cpp
    src/Project.cpp
    src/RelatedFormats.cpp
    src/Environment.cpp
    src/OptionSettings.cpp
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

set(_tekladb1_occt_uses_package OFF)
if(TEKLADB1_WITH_OCCT)
    if(TEKLADB1_OCCT_INCLUDE_DIR AND TEKLADB1_OCCT_LIBRARIES)
        set(_tekladb1_occt_include "${TEKLADB1_OCCT_INCLUDE_DIR}")
        set(_tekladb1_occt_libraries ${TEKLADB1_OCCT_LIBRARIES})
    else()
        set(_tekladb1_occt_uses_package ON)
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
    add_executable(tekladb1_analysis_settings_test tests/analysis_settings.cpp)
    target_link_libraries(tekladb1_analysis_settings_test PRIVATE TeklaFormats::core)
    foreach(case analysis_settings_fields analysis_settings_numbers analysis_settings_quotes analysis_settings_encoding analysis_settings_design analysis_settings_header analysis_settings_limits)
        add_test(NAME related.${case} COMMAND tekladb1_analysis_settings_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    add_executable(tekladb1_analysis_raw_test tests/analysis_raw.cpp)
    target_link_libraries(tekladb1_analysis_raw_test PRIVATE TeklaFormats::core)
    foreach(case analysis_raw_roundtrip analysis_raw_prefix analysis_raw_header analysis_raw_tables analysis_raw_truncated analysis_raw_limits)
        add_test(NAME related.${case} COMMAND tekladb1_analysis_raw_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    add_executable(tekladb1_drawing_metadata_test tests/drawing_metadata.cpp)
    target_link_libraries(tekladb1_drawing_metadata_test PRIVATE TeklaFormats::core)
    foreach(case drawing_metadata_fields drawing_metadata_scope drawing_metadata_entities drawing_metadata_numbers drawing_metadata_guid drawing_metadata_declarations drawing_metadata_limits)
        add_test(NAME related.${case} COMMAND tekladb1_drawing_metadata_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    add_executable(tekladb1_guid_mapping_test tests/guid_mappings.cpp)
    target_link_libraries(tekladb1_guid_mapping_test PRIVATE TeklaFormats::core)
    foreach(case mapper_text mapper_duplicates mapper_failure mapper_targets)
        add_test(NAME related.${case} COMMAND tekladb1_guid_mapping_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    add_executable(tekladb1_shapes_test tests/shapes.cpp)
    target_link_libraries(tekladb1_shapes_test PRIVATE TeklaFormats::core ZLIB::ZLIB)
    foreach(case shapes_scope shapes_geometry shapes_numbers shapes_structure shapes_entities shapes_limits shapes_gzip shapes_catalog)
        add_test(NAME related.${case} COMMAND tekladb1_shapes_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    add_executable(tekladb1_metadata_test tests/metadata.cpp)
    target_link_libraries(tekladb1_metadata_test PRIVATE TeklaFormats::core)
    foreach(case metadata_scope metadata_entities metadata_boolean metadata_structure metadata_declarations metadata_encoding metadata_limits)
        add_test(NAME related.${case} COMMAND tekladb1_metadata_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
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
    add_executable(tekla_related_test tests/related.cpp)
    target_link_libraries(tekla_related_test PRIVATE TeklaFormats::core)
    add_executable(tekla_environment_test tests/environment.cpp)
    target_link_libraries(tekla_environment_test PRIVATE TeklaFormats::core)
    add_executable(tekla_option_settings_test tests/option_settings.cpp)
    target_link_libraries(tekla_option_settings_test PRIVATE TeklaFormats::core)
    foreach(case settings_text settings_failure settings_matching settings_unknown)
        add_test(NAME related.${case} COMMAND tekla_option_settings_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    foreach(case environment_valid options_valid dbv_embedded_magic dbv_truncations
            environment_invalid options_invalid dbv_unknown_rows dbv_limit)
        add_test(NAME related.${case} COMMAND tekla_environment_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    foreach(case numbering_valid numbering_truncated numbering_invalid
            drawing_header_boundary drawing895_valid drawing895_signature drawing895_missing drawing895_cycle
            drawing895_sheet_nan drawing895_subject_duplicate drawing895_subject_zero drawing895_subject_type
            drawing895_reference_duplicate drawing895_guid drawing895_property drawing895_version drawing895_truncated
            drawing908_valid drawing908_signature drawing908_reference drawing908_missing drawing908_cycle
            drawing908_sheet_nan drawing908_subject_duplicate drawing908_subject_zero drawing908_subject_type
            drawing908_reference_duplicate drawing908_guid drawing908_property drawing908_version drawing908_truncated
            numbering908_valid numbering908_empty numbering908_width numbering908_guid numbering908_duplicate
            numbering908_key numbering908_tail numbering908_truncated numbering908_unknown_table
            numbering730_valid numbering730_empty numbering730_truncated numbering730_header numbering730_width numbering730_invalid
            drawing_valid drawing_cycle
            drawing_missing_chunk drawing_duplicate drawing_missing_property drawing_signature
            drawing_nonfinite drawing_truncated drawing_limit numbering_unknown drawing_unknown)
        add_test(NAME related.${case} COMMAND tekla_related_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    foreach(case dimensions_valid dimensions_angled dimensions_deferred dimensions_nonplanar dimensions_unknown dimensions_invalid dimensions_legacy
        dimensions730_valid dimensions730_angled dimensions730_deferred dimensions730_nonplanar dimensions730_unknown dimensions730_invalid dimensions730_ambiguous dimensions730_collapsed
            drawing730_valid drawing730_signature drawing730_cycle drawing730_missing drawing730_property
            drawing730_nonfinite drawing730_volume drawing730_duplicate drawing730_context drawing730_collapsed
            drawing730_subject_unknown drawing730_general drawing730_truncated drawing730_limit
            drawing_view_valid drawing_view_duplicate drawing_view_basis drawing_view_nonfinite
            drawing_view_volume drawing_view_depth drawing_view_subject drawing_view_unknown
            drawing844_valid drawing844_signature drawing844_reference drawing844_missing drawing844_cycle
            drawing844_guid drawing844_conflict drawing844_property drawing844_nonfinite drawing844_header
            drawing844_descriptor drawing844_zero_descriptor drawing844_truncated drawing844_limit
            drawing782_valid drawing782_missing drawing782_cycle drawing782_fields drawing782_context
            drawing782_collapsed drawing782_nonfinite drawing782_conflict drawing782_truncated)
        add_test(NAME related.${case} COMMAND tekla_related_test "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    foreach(case 764_model 764_library 764_library_fields 764_fields 764_width
            764_part_join 764_contour_join 764_string_cycle 764_missing_string 764_bolt_join
            764_numeric_nan 764_numeric_overflow 764_assembly_missing 764_library_definition_fields 764_library_definition_width
            764_library_definition_string 764_library_definition_identity 764_library_definition_duplicate 764_library_parameter_string 764_library_parameter_identity
            764_library_parameter_duplicate 764_library_distance_fields 764_library_distance_width 764_library_distance_string 764_library_distance_identity
            764_library_distance_nan 764_library_distance_secondary_nan 764_library_distance_bindings 764_library_distance_duplicate 764_library_formula_fields
            764_library_formula_width 764_library_formula_string 764_library_formula_target 764_library_formula_identity 764_library_formula_duplicate
            764_library_formula_refs 764_library_formula_output 764_weld 764_library_weld 764_weld_fields
            764_weld_width 764_wrong_count 764_wrong_weld_table)
        add_test(NAME db1.${case} COMMAND tekladb1_regression "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    foreach(case bolt844_valid bolt844_library bolt844_multi bolt844_library_multi bolt844_empty bolt844_null
            bolt844_missing_definition bolt844_missing_point bolt844_missing_frame bolt844_missing_array bolt844_missing_identity
            bolt844_duplicate_group bolt844_duplicate_definition bolt844_origin_nan bolt844_position_nan bolt844_diameter_nan
            bolt844_count bolt844_sequence bolt844_group_fields bolt844_definition_fields
            profile844_contour profile844_valid profile844_library profile844_boundary profile844_library_boundary
            profile844_missing profile844_next profile844_cycle profile844_library_cycle
            valid_model short_trailer unknown_version wrong_fields broken_reference
            unhandled_part unverified_contour embedded_magic plain_limit gzip_limit
            gzip_concat gzip_corrupt gzip_truncated gzip_garbage truncations unicode_path
            ambiguity raw_retention project strict_companions catalog_error model_limit
            settings_project settings_project_no_raw settings_project_off settings_project_invalid settings_project_strict
            mapper_project mapper_project_gzip mapper_project_no_raw mapper_project_off mapper_project_invalid mapper_project_strict
            dbv_name project_precedence partial_project gzip_large duplicate_id nonfinite project_guid
            project_shapes_pair project_shapes_sibling project_shapes_internal project_shapes_precedence
            project_shapes_lower_conflict project_shapes_missing project_shapes_split project_shapes_malformed project_shapes_strict
            schema908_valid schema908_library schema908_bolt schema908_library_bolt schema908_unknown_kind
            schema908_bad_class schema908_library_bad_class schema908_bad_owner schema908_bad_signature
            schema908_bad_bolt_signature schema908_bad_contour_signature schema908_missing_string schema908_string_cycle
            schema908_bad_point schema908_nonfinite schema908_wrong_count
            schema908_bolt_missing_identity schema908_bolt_duplicate schema908_bolt_wrong_kind
            schema908_bolt_missing_placement schema908_bolt_origin_nan
            782_model 782_library 782_library_fields 782_fields 782_width 782_part_join 782_contour_join
            782_string_cycle 782_missing_string 782_bolt_join 782_numeric_nan 782_numeric_overflow 782_assembly_missing
            782_library_definition_fields 782_library_definition_width 782_library_definition_string
            782_library_definition_identity 782_library_definition_duplicate 782_library_parameter_string
            782_library_parameter_identity 782_library_parameter_duplicate
            782_library_distance_fields 782_library_distance_width 782_library_distance_string 782_library_distance_identity
            782_library_distance_nan 782_library_distance_secondary_nan 782_library_distance_bindings 782_library_distance_duplicate
            782_library_formula_fields 782_library_formula_width 782_library_formula_string 782_library_formula_target
            782_library_formula_identity 782_library_formula_duplicate 782_library_formula_refs 782_library_formula_output
            ownership_895 ownership_modern ownership_895_class_fields ownership_895_class_width ownership_895_class_missing
            ownership_895_class_duplicate ownership_895_owner_missing ownership_aux_fields ownership_aux_missing ownership_aux_duplicate
            ownership_distance_fields ownership_formula_fields ownership_formula_cycle ownership_formula_missing_chunk ownership_other_scope
            ownership_custom_reference_missing ownership_position_start_nan ownership_position_end_inf
            ownership_position_depth_nan ownership_position_plane_inf ownership_position_unknown
            inline_position_main inline_position_library inline_position_start_nan inline_position_end_inf
            inline_position_depth_nan inline_position_plane_inf inline_position_unused_nan inline_position_unknown
            object_number_main object_number_library object_number_895 object_number_zero object_number_special object_number_overflow
            object_number_unknown object_number_null object_number_missing object_number_identity object_number_duplicate
            object_number_collision object_number_duplicate_link object_number_fields object_number_link_fields object_number_width object_number_link_width
            object_number_project object_number_project_guid object_number_project_no_guid object_number_project_absent
            object_number_project_series object_number_project_ambiguous object_number_project_library_scope
            reinforcement_main reinforcement_library reinforcement_895 reinforcement_null
            reinforcement_duplicate reinforcement_duplicate_definition reinforcement_definition reinforcement_identity reinforcement_kind reinforcement_missing_instance
            reinforcement_father reinforcement_father_target reinforcement_father_duplicate reinforcement_placement reinforcement_nonfinite_placement
            reinforcement_array_missing reinforcement_array_cycle reinforcement_array_tail reinforcement_array_count reinforcement_integer_count
            reinforcement_array_duplicate reinforcement_nonfinite reinforcement_coordinate_count reinforcement_string
            reinforcement_fields reinforcement_width reinforcement_array_fields
            reinforcement_shared reinforcement_budget reinforcement_budget_override reinforcement_duplicate_placement
            bolt_position_null bolt_position_library bolt_position_present bolt_position_missing
            bolt_position_point bolt_position_definition bolt_position_placement bolt_position_empty_array
            rebar_number_main rebar_number_library rebar_number_895 rebar_number_952
            rebar_number_zero rebar_number_special rebar_number_raw_zero rebar_number_raw_high rebar_number_full_prefix
            rebar_number_unreferenced rebar_number_null rebar_number_shared rebar_number_wrong_kind rebar_number_wrong_kind895
            rebar_number_missing rebar_number_fields rebar_number_width rebar_number_duplicate rebar_number_zero_id rebar_number_project
            inline_number_730_main inline_number_730_library inline_number_730_zero inline_number_730_non1 inline_number_730_zero_start
            inline_number_730_special inline_number_730_tag inline_number_730_tag12 inline_number_730_duplicate inline_number_730_missing_record
            inline_number_730_identity inline_number_730_wrong_kind inline_number_730_zero_id inline_number_730_width inline_number_730_empty
            inline_number_730_project_default inline_number_730_project_trusted inline_number_730_project_missing_series
            inline_number_730_project_ambiguous inline_number_730_project_absent inline_number_730_project_version inline_number_730_project_library_scope
            legacy_number_main legacy_number_library legacy_number_fields legacy_number_width legacy_number_link_fields
            legacy_number_link_width legacy_number_duplicate legacy_number_collision legacy_number_duplicate_link
            legacy_number_missing_identity legacy_number_missing_target legacy_number_zero legacy_number_special
            legacy_number_project_default legacy_number_project_trusted legacy_number_project_conflict legacy_number_project_modern_pair
            surface_main surface_library surface_unknown_profile surface_fields surface_definition_fields surface_width
            surface_definition_missing surface_point_missing surface_frame_missing surface_contour_missing surface_identity_missing
            surface_father_missing surface_father_unknown surface_father_duplicate surface_target_missing surface_duplicate
            surface_definition_duplicate surface_origin_nan surface_length_inf surface_contour_nan surface_chamfer_nan surface_project_no_catalog)
        add_test(NAME db1.${case} COMMAND tekladb1_regression "${CMAKE_CURRENT_BINARY_DIR}/test-data" ${case})
    endforeach()
    if(TEKLADB1_WITH_OCCT)
        add_executable(tekladb1_geometry_test tests/geometry.cpp)
        target_link_libraries(tekladb1_geometry_test PRIVATE TeklaDB1::occt)
        add_test(NAME db1.geometry_volumes COMMAND tekladb1_geometry_test)
        add_test(NAME db1.geometry_nominal_sections COMMAND tekladb1_geometry_test nominal)
        add_test(NAME db1.geometry_polybeam_miters COMMAND tekladb1_geometry_test miter)
        add_test(NAME db1.geometry_cambering COMMAND tekladb1_geometry_test camber)
        add_test(NAME db1.geometry_ellipses COMMAND tekladb1_geometry_test ellipse)
        add_test(NAME db1.geometry_bolt844_deferred COMMAND tekladb1_geometry_test bolt844)
        add_test(NAME db1.geometry_bolt908_deferred COMMAND tekladb1_geometry_test bolt908)
        add_executable(tekladb1_ifc_geometry tests/ifc_geometry.cpp)
        target_link_libraries(tekladb1_ifc_geometry PRIVATE TeklaDB1::occt)
        add_test(NAME db1.ifc_volume_evidence COMMAND tekladb1_ifc_geometry ifc_volume_evidence)
        if(MINGW)
            target_link_options(tekladb1_ifc_geometry PRIVATE -municode)
        endif()
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
install(FILES third_party/tinyxml2/LICENSE.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/TeklaDB1/licenses/tinyxml2)
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
