option(ISODUO_REQUIRE_CLANG_TIDY "Fail configuration when clang-tidy is unavailable" OFF)
get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(ISODUO_CLANG_TIDY NAMES clang-tidy HINTS "${compiler_dir}")

if(ISODUO_CLANG_TIDY)
  foreach(test IN ITEMS identifier_naming identifier_naming_fixtures)
    add_test(NAME ${test} COMMAND "${CMAKE_COMMAND}"
      "-DCLANG_TIDY=${ISODUO_CLANG_TIDY}"
      "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      -P "${CMAKE_CURRENT_LIST_DIR}/${test}.cmake")
  endforeach()
elseif(ISODUO_REQUIRE_CLANG_TIDY)
  message(FATAL_ERROR "clang-tidy is required for the identifier_naming CTest")
else()
  add_executable(isoduo_skip_identifier_naming "${CMAKE_CURRENT_LIST_DIR}/identifier_naming/skip.cpp")
  isoduo_target(isoduo_skip_identifier_naming)
  add_test(NAME identifier_naming COMMAND isoduo_skip_identifier_naming)
  set_tests_properties(identifier_naming PROPERTIES SKIP_RETURN_CODE 77)
endif()
