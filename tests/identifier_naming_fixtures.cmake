cmake_minimum_required(VERSION 3.25)

set(fixtures "${CMAKE_CURRENT_LIST_DIR}/identifier_naming")
file(MAKE_DIRECTORY "${BUILD_DIR}/identifier_naming")
foreach(sample IN ITEMS bad allowed)
  configure_file("${fixtures}/${sample}.cpp.txt"
    "${BUILD_DIR}/identifier_naming/${sample}.cpp" COPYONLY)
  execute_process(
    COMMAND "${CLANG_TIDY}" "--config-file=${SOURCE_DIR}/.clang-tidy"
      "${BUILD_DIR}/identifier_naming/${sample}.cpp" -- -std=c++20 -isystem "${fixtures}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(sample STREQUAL "bad")
    if(NOT result EQUAL 1)
      message(FATAL_ERROR "Expected clang-tidy to reject bad names. Exit ${result}\n${output}${errors}")
    endif()
    foreach(name IN ITEMS CreateBuffer entryCount maxEntries snake_case)
      if(NOT output MATCHES "invalid case style for [^\n]* '${name}'")
        message(FATAL_ERROR "Missing diagnostic for ${name}\n${output}${errors}")
      endif()
    endforeach()
  elseif(NOT result EQUAL 0)
    message(FATAL_ERROR "Expected clang-tidy to accept allowed names. Exit ${result}\n${output}${errors}")
  endif()
endforeach()
