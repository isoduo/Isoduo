cmake_minimum_required(VERSION 3.25)
set(checker "${CMAKE_CURRENT_LIST_DIR}/source_text.cmake")
string(RANDOM LENGTH 12 suffix)
set(root "${CMAKE_CURRENT_BINARY_DIR}/source_text_fixture_${suffix}")
file(MAKE_DIRECTORY "${root}/src" "${root}/examples/third_party" "${root}/tests/build")

function(expect_failure bytes diagnostic)
  file(WRITE "${root}/src/example.cpp" "\n${bytes}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}" -P "${checker}"
    RESULT_VARIABLE result ERROR_VARIABLE output)
  string(FIND "${output}" "${diagnostic}" found)
  if(result EQUAL 0 OR found EQUAL -1)
    message(FATAL_ERROR "Expected '${diagnostic}' in: ${output}")
  endif()
endfunction()

# Invalid sequences include overlong encodings, surrogates, out-of-range code
# points, stray continuation bytes and a truncated multibyte character.
foreach(sequence IN ITEMS "192 175" "224 159 191" "237 160 128" "240 143 191 191"
                          "244 144 128 128" "128" "226 130")
  separate_arguments(bytes UNIX_COMMAND "${sequence}")
  string(ASCII ${bytes} invalid)
  expect_failure("${invalid}" "src/example.cpp: not valid UTF-8")
endforeach()
string(ASCII 239 191 189 replacement)
expect_failure("${replacement}" "src/example.cpp:2: U+FFFD")
string(REPEAT "?" 3 lost)
expect_failure("\"${lost}\"" "src/example.cpp:2: three or more question marks")

file(WRITE "${root}/src/example.cpp" "u8\"中文 😀\"")
file(WRITE "${root}/README.md" "\"${lost}\"")
file(WRITE "${root}/examples/third_party/vendor.cpp" "${invalid}")
file(WRITE "${root}/tests/build/generated.hpp" "${replacement}")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}" -P "${checker}" COMMAND_ERROR_IS_FATAL ANY)
file(REMOVE_RECURSE "${root}")
include("${checker}")
