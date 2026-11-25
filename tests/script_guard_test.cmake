cmake_minimum_required(VERSION 3.25)
set(checker "${CMAKE_CURRENT_LIST_DIR}/script_guard.cmake")
set(fixture "${CMAKE_CURRENT_BINARY_DIR}/script-guard-list.txt")
file(WRITE "${fixture}" "src/main.cpp\nnotes.py.md\nscript.sh/data.txt\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DFILE_LIST=${fixture}" -P "${checker}"
  COMMAND_ERROR_IS_FATAL ANY)

set(forbidden "tools/example.ps1" "old scripts/run.sh" "test.PSM1" "demo.py" "go.bat" "go.cmd")
foreach(path IN LISTS forbidden)
  file(APPEND "${fixture}" "${path}\n")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DFILE_LIST=${fixture}" -P "${checker}"
  RESULT_VARIABLE result ERROR_VARIABLE diagnostic)
if(result EQUAL 0)
  message(FATAL_ERROR "Accepted a list containing scripts")
endif()
foreach(path IN LISTS forbidden)
  string(FIND "${diagnostic}" "${path}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Expected ${path} in the diagnostic: ${diagnostic}")
  endif()
endforeach()

# Simulate a checkout with an index on a machine without Git.
set(checkout "${CMAKE_CURRENT_BINARY_DIR}/script-guard-checkout")
file(MAKE_DIRECTORY "${checkout}/.git" "${checkout}/tests")
file(COPY "${checker}" DESTINATION "${checkout}/tests")
execute_process(COMMAND "${CMAKE_COMMAND}" -DCMAKE_DISABLE_FIND_PACKAGE_Git=ON
  -P "${checkout}/tests/script_guard.cmake" RESULT_VARIABLE result OUTPUT_VARIABLE output)
if(result OR NOT output MATCHES "Skipping tracked script check: Git executable unavailable")
  message(FATAL_ERROR "Expected missing Git to skip the tracked script check: ${output}")
endif()
