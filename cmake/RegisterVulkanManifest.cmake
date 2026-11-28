cmake_minimum_required(VERSION 3.25)
# The hosted Windows loader uses machine-wide manifest registration.
if(NOT "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")
  message(FATAL_ERROR "Manifest registration is only for disposable GitHub-hosted runners")
endif()
if(NOT KIND MATCHES "^(Drivers|ExplicitLayers)$" OR NOT EXISTS "${MANIFEST}")
  message(FATAL_ERROR "Provide a driver or explicit layer manifest")
endif()
file(REAL_PATH "${MANIFEST}" absolute_manifest)
cmake_path(NATIVE_PATH absolute_manifest manifest_path)
execute_process(COMMAND reg.exe add "HKLM\\SOFTWARE\\Khronos\\Vulkan\\${KIND}"
  /v "${manifest_path}" /t REG_DWORD /d 0 /f /reg:64 COMMAND_ERROR_IS_FATAL ANY)
