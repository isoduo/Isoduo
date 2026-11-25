cmake_minimum_required(VERSION 3.25)

if(NOT IS_DIRECTORY "${DESTINATION}" OR "$ENV{RUNNER_TEMP}" STREQUAL "" OR
   "$ENV{MESA_URL}" STREQUAL "" OR "$ENV{MESA_SHA256}" STREQUAL "")
  message(FATAL_ERROR "Set DESTINATION to the build directory and provide RUNNER_TEMP, MESA_URL and MESA_SHA256")
endif()

set(work "$ENV{RUNNER_TEMP}/isoduo-mesa")
file(MAKE_DIRECTORY "${work}")
set(archive "${work}/mesa.7z")
foreach(attempt RANGE 1 4)
  file(DOWNLOAD "$ENV{MESA_URL}" "${archive}" STATUS status TLS_VERIFY ON)
  list(GET status 0 code)
  if(code EQUAL 0)
    break()
  elseif(attempt EQUAL 4)
    message(FATAL_ERROR "Mesa download failed: ${status}")
  endif()
  math(EXPR delay "15 * ${attempt}")
  message(WARNING "Mesa download attempt ${attempt} failed: ${status}. Retrying in ${delay}s")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep "${delay}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()

file(SHA256 "${archive}" actual)
if(NOT actual STREQUAL "$ENV{MESA_SHA256}")
  message(FATAL_ERROR "Mesa SHA256 mismatch: ${actual}")
endif()

file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${work}"
     PATTERNS "x64/opengl32.dll" "x64/libgallium_wgl.dll" "x64/vulkan_lvp.dll" "x64/lvp_icd.x86_64.json")
file(COPY "${work}/x64/opengl32.dll" "${work}/x64/libgallium_wgl.dll"
     "${work}/x64/vulkan_lvp.dll" "${work}/x64/lvp_icd.x86_64.json" DESTINATION "${DESTINATION}")
