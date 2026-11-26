option(ISODUO_REQUIRE_CLANG_TIDY "Require the Clang identifier naming tools for local builds" OFF)
get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(ISODUO_CLANG_TIDY NAMES clang-tidy HINTS "${compiler_dir}" "C:/Program Files/LLVM/bin")
get_filename_component(tidy_dir "${ISODUO_CLANG_TIDY}" DIRECTORY)
find_program(ISODUO_CLANG_QUERY NAMES clang-query HINTS "${tidy_dir}" "${compiler_dir}")

if(ISODUO_CLANG_TIDY AND ISODUO_CLANG_QUERY)
  foreach(test IN ITEMS identifier_naming identifier_naming_fixtures)
    add_test(NAME ${test} COMMAND "${CMAKE_COMMAND}"
      "-DCLANG_TIDY=${ISODUO_CLANG_TIDY}"
      "-DCLANG_QUERY=${ISODUO_CLANG_QUERY}"
      "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      -P "${CMAKE_CURRENT_LIST_DIR}/${test}.cmake")
  endforeach()
elseif(ISODUO_REQUIRE_CLANG_TIDY OR "$ENV{CI}")
  message(FATAL_ERROR "clang-tidy and clang-query are required for the identifier_naming CTest")
else()
  add_executable(isoduo_skip_identifier_naming "${CMAKE_CURRENT_LIST_DIR}/identifier_naming/skip.cpp")
  isoduo_target(isoduo_skip_identifier_naming)
  add_test(NAME identifier_naming COMMAND isoduo_skip_identifier_naming)
  set_tests_properties(identifier_naming PROPERTIES SKIP_RETURN_CODE 77)
endif()
