cmake_minimum_required(VERSION 3.25)

if(NOT EXISTS "${BUILD_DIR}/compile_commands.json")
  message(FATAL_ERROR "identifier_naming needs compile_commands.json. Configure with a Ninja or Makefiles preset.")
endif()
file(READ "${BUILD_DIR}/compile_commands.json" database)
string(JSON count LENGTH "${database}")
set(sources)
set(index 0)
while(index LESS count)
  string(JSON source GET "${database}" ${index} file)
  string(JSON directory GET "${database}" ${index} directory)
  math(EXPR index "${index} + 1")
  cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${directory}" NORMALIZE)
  cmake_path(IS_PREFIX SOURCE_DIR "${source}" NORMALIZE isProjectSource)
  if(isProjectSource)
    cmake_path(RELATIVE_PATH source BASE_DIRECTORY "${SOURCE_DIR}")
    if(source MATCHES "^(src|include|tests|examples)/.*[.](cpp|cc|cxx|c)$" AND
       NOT source MATCHES "(^|/)third_party/")
      list(APPEND sources "${source}")
    endif()
  endif()
endwhile()
list(REMOVE_DUPLICATES sources)
if(NOT sources)
  message(FATAL_ERROR "No Isoduo translation units in ${BUILD_DIR}/compile_commands.json")
endif()

# Clang uses backslashes in Windows diagnostic paths. Match either separator.
string(REGEX REPLACE "([][+.*()?^$|])" "\\\\\\1" sourceFilter "${SOURCE_DIR}")
string(REPLACE "/" "[/\\\\]" sourceFilter "${sourceFilter}")
set(projectHeaders "^${sourceFilter}[/\\\\](include|src|tests|examples)[/\\\\]")
list(LENGTH sources count)
message(STATUS "Checking identifier names in ${count} Isoduo translation units and their headers")
execute_process(
  COMMAND "${CLANG_TIDY}" "--config-file=${SOURCE_DIR}/.clang-tidy"
    "-p=${BUILD_DIR}" "--header-filter=${projectHeaders}" ${sources}
  WORKING_DIRECTORY "${SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)

include("${CMAKE_CURRENT_LIST_DIR}/private_static_naming.cmake")
# clang-query retains every AST in a single invocation. Bound its memory use.
math(EXPR last "${count} - 1")
foreach(offset RANGE 0 ${last} 8)
  list(SUBLIST sources ${offset} 8 batch)
  find_private_static_naming_violations(violations "${projectHeaders}" "-p=${BUILD_DIR}" ${batch})
  if(violations)
    message(SEND_ERROR "${violations}")
  endif()
endforeach()
