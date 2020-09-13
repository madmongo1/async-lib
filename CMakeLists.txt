set(lib_name util)
set(namespace_name notstd)
set(lib_dependencies notstd::config Boost::json fmt::fmt spdlog::spdlog)
set(lib_definitions )
project("${namespace_name}_${lib_name}" VERSION 0.1)

file(GLOB_RECURSE
        src_files
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "src/*.cpp" "src/*.hpp")
file(GLOB_RECURSE
        generated_hdr_files
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/include/*.hpp")
file(GLOB_RECURSE
        hdr_files
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "include/*.hpp")
list(APPEND hdr_files ${generated_hdr_files})
set(spec_files ${src_files})
list(FILTER spec_files INCLUDE REGEX "^.*.\\.spec\\.cpp$")
list(FILTER src_files EXCLUDE REGEX "^.*.\\.spec\\.cpp$")

if ("${src_files}" STREQUAL "")
    add_library(${PROJECT_NAME} INTERFACE)
    set(IFACE_OR_PUBLIC "INTERFACE")
    set(is_interface_lib ON)
    message(STATUS "[${PROJECT_NAME}] is INTERFACE library")
else()
    add_library(${PROJECT_NAME} ${src_files} ${hdr_files})
    set(IFACE_OR_PUBLIC "PUBLIC")
    set(is_interface_lib OFF)
    message(STATUS "[${PROJECT_NAME}] is PUBLIC library")
endif()
target_link_libraries(${PROJECT_NAME} ${IFACE_OR_PUBLIC} ${lib_dependencies})
target_include_directories(${PROJECT_NAME}
        ${IFACE_OR_PUBLIC}
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include")
if (NOT is_interface_lib)
    target_include_directories(${PROJECT_NAME}
            PRIVATE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>"
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME "${lib_name}")
add_library("${namespace_name}::${lib_name}" ALIAS "${PROJECT_NAME}")

list(APPEND all_libs ${PROJECT_NAME})
list(APPEND all_spec_files ${spec_files})

if (NOT "${spec_files}" STREQUAL "")
    add_executable("${PROJECT_NAME}_test" ${spec_files})
    target_link_libraries("${PROJECT_NAME}_test" "${namespace_name}::${lib_name}")
    add_test("Test${PROJECT_NAME}" COMMAND "${PROJECT_NAME}_test")
endif()

set(all_libs "${all_libs}" PARENT_SCOPE)
set(all_spec_files "${all_spec_files}" PARENT_SCOPE)
