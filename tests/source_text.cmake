cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED ROOT)
  get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

# Tag each byte in the hex dump so that matches stay on byte boundaries,
# including after embedded NULs. Restrict the second byte for E0/ED/F0/F4
# to exclude overlong encodings, UTF-16 surrogates and values above U+10FFFF.
set(tail "x[89ab][0-9a-f]")
string(CONCAT scalar
  "x[0-7][0-9a-f]|"
  "x(c[2-9a-f]|d[0-9a-f])${tail}|"
  "x(e0x[ab][0-9a-f]|edx[89][0-9a-f]|e[1-9abcef]${tail})${tail}|"
  "x(f0x[9ab][0-9a-f]|f[1-3]${tail}|f4x8[0-9a-f])${tail}${tail}")

file(GLOB files LIST_DIRECTORIES false RELATIVE "${ROOT}" "${ROOT}/*")
foreach(dir IN ITEMS src include tests examples cmake .github)
  file(GLOB_RECURSE nested RELATIVE "${ROOT}" "${ROOT}/${dir}/*")
  list(APPEND files ${nested})
endforeach()
foreach(path IN LISTS files)
  string(TOLOWER "${path}" lower_path)
  if(lower_path MATCHES "(^|/)(third_party|build)/" OR
     NOT lower_path MATCHES "\\.(cpp|hpp|h|ps1|cmake|md|txt|tsv|json|yml)$")
    continue()
  endif()
  file(READ "${ROOT}/${path}" hex HEX)
  string(REGEX REPLACE "(..)" "x\\1" bytes "${hex}")
  string(REGEX REPLACE "${scalar}" "" invalid "${bytes}")
  if(NOT invalid STREQUAL "")
    message(SEND_ERROR "${path}: not valid UTF-8")
    continue()
  endif()

  set(check_strings FALSE)
  if(lower_path MATCHES "\\.(cpp|hpp|h|ps1|cmake)$" AND bytes MATCHES "x3fx3fx3f")
    set(check_strings TRUE)
  endif()
  if(NOT check_strings AND NOT bytes MATCHES "xefxbfxbd")
    continue()
  endif()
  string(REPLACE "x0a" ";" lines "${bytes}")
  set(number 0)
  foreach(line IN LISTS lines)
    math(EXPR number "${number} + 1")
    if(line MATCHES "xefxbfxbd")
      message(SEND_ERROR "${path}:${number}: U+FFFD replacement character")
    endif()
    if(check_strings AND line MATCHES "x3fx3fx3f")
      string(REPLACE "x22" "\"" quoted "${line}")
      if(quoted MATCHES "\"[^\"]*x3fx3fx3f")
        message(SEND_ERROR "${path}:${number}: three or more question marks inside a string literal")
      endif()
    endif()
  endforeach()
endforeach()
