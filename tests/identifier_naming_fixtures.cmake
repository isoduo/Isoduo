cmake_minimum_required(VERSION 3.25)

set(fixtures "${CMAKE_CURRENT_LIST_DIR}/identifier_naming")
include("${CMAKE_CURRENT_LIST_DIR}/private_static_naming.cmake")
file(MAKE_DIRECTORY "${BUILD_DIR}/identifier_naming")
configure_file("${fixtures}/bad_members.hpp.txt" "${BUILD_DIR}/identifier_naming/bad_members.hpp" COPYONLY)
foreach(sample IN ITEMS bad allowed)
  configure_file("${fixtures}/${sample}.cpp.txt"
    "${BUILD_DIR}/identifier_naming/${sample}.cpp" COPYONLY)
  execute_process(
    COMMAND "${CLANG_TIDY}" "--config-file=${SOURCE_DIR}/.clang-tidy"
      "${BUILD_DIR}/identifier_naming/${sample}.cpp" -- -std=c++20 -isystem "${fixtures}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  find_private_static_naming_violations(violations "[/\\\\]identifier_naming[/\\\\](${sample}[.]cpp|bad_members[.]hpp)$"
    "${BUILD_DIR}/identifier_naming/${sample}.cpp" -- -std=c++20 -isystem "${fixtures}")
  if(sample STREQUAL "bad")
    if(NOT result EQUAL 1)
      message(SEND_ERROR "Expected clang-tidy to reject bad names. Exit ${result}\n${output}${errors}")
    endif()
    foreach(name IN ITEMS CreateBuffer entryCount maxEntries snake_case)
      if(NOT output MATCHES "invalid case style for [^\n]* '${name}'")
        message(SEND_ERROR "Missing diagnostic for ${name}\n${output}${errors}")
      endif()
    endforeach()
    foreach(name IN ITEMS capacity sharedCount openGL)
      if(NOT violations MATCHES "int ${name} =")
        message(SEND_ERROR "Missing private static member diagnostic for ${name}\n${violations}")
      endif()
    endforeach()
  elseif(NOT result EQUAL 0)
    message(SEND_ERROR "Expected clang-tidy to accept allowed names. Exit ${result}\n${output}${errors}")
  elseif(violations)
    message(SEND_ERROR "Expected clang-query to accept allowed names\n${violations}")
  endif()
endforeach()

foreach(missing IN ITEMS CLANG_TIDY CLANG_QUERY)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env CI=true
      "${CMAKE_COMMAND}" -S "${fixtures}/configure" -B "${BUILD_DIR}/identifier_naming/configure"
      "-DSOURCE_DIR=${SOURCE_DIR}" "-DISODUO_CLANG_TIDY=${CLANG_TIDY}"
      "-DISODUO_CLANG_QUERY=${CLANG_QUERY}" "-DISODUO_${missing}="
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(result EQUAL 0 OR NOT errors MATCHES "clang-tidy and clang-query are required")
    message(SEND_ERROR "Expected CI configuration to reject missing ${missing}. Exit ${result}\n${output}${errors}")
  endif()
endforeach()
