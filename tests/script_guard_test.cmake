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

# Exercise CTest's result classification as well as the checker's exit status.
set(probe "${CMAKE_CURRENT_BINARY_DIR}/script-guard-ctest")
file(MAKE_DIRECTORY "${probe}")
file(WRITE "${probe}/paths.txt" "Skipping tracked script check: forbidden.py\n")
macro(run_guard option script)
  file(WRITE "${probe}/CTestTestfile.cmake"
    "set(CMAKE_COMMAND [=[${CMAKE_COMMAND}]=])\n"
    "include([=[${CMAKE_CURRENT_LIST_DIR}/register_script_guard.cmake]=])\n"
    "register_script_guard([=[${script}]=] [=[${option}]=])\n")
  execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${probe}" -V
    RESULT_VARIABLE result OUTPUT_VARIABLE output)
endmacro()
run_guard("-DFILE_LIST=${probe}/paths.txt" "${checker}")
if(NOT result OR NOT output MATCHES "Forbidden tracked script files")
  message(FATAL_ERROR "Expected CTest to fail for the forbidden path: ${output}")
endif()

# Simulate a checkout with an index on a machine without Git.
set(checkout "${CMAKE_CURRENT_BINARY_DIR}/script-guard-checkout")
file(MAKE_DIRECTORY "${checkout}/.git" "${checkout}/tests")
file(COPY "${checker}" DESTINATION "${checkout}/tests")
run_guard("-DCMAKE_DISABLE_FIND_PACKAGE_Git=ON" "${checkout}/tests/script_guard.cmake")
if(result OR NOT output MATCHES "guard [(]Skipped[)]" OR NOT output MATCHES "Skipping tracked script check: Git executable unavailable")
  message(FATAL_ERROR "Expected missing Git to skip the tracked script check: ${output}")
endif()
