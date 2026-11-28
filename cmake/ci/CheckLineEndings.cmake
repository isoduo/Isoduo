cmake_minimum_required(VERSION 3.25)
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" ls-files --eol
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
  OUTPUT_VARIABLE entries COMMAND_ERROR_IS_FATAL ANY)

# Keep each offending Git record intact, including paths with CMake list separators.
string(REGEX REPLACE "\ni/(lf|none|-text) [^\n]*" "" offenders "\n${entries}")
if(offenders MATCHES "\ni/(crlf|mixed) ")
  message(FATAL_ERROR "Tracked files with CRLF or mixed endings in the Git index:${offenders}")
endif()
