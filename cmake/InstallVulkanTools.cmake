cmake_minimum_required(VERSION 3.25)
set(work "$ENV{RUNNER_TEMP}/isoduo-vulkan-tools")
file(MAKE_DIRECTORY "${work}")
set(sdk "${work}/sdk")
file(DOWNLOAD
  "https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/vulkansdk-windows-X64-1.4.363.0.exe"
  "${work}/sdk.exe" TLS_VERIFY ON
  EXPECTED_HASH SHA256=94a82d378f7a5e3e54c9db7d2fb7016af136e14ac0a18dbf0f2f67a36352d141)
execute_process(COMMAND "${work}/sdk.exe" --root "${sdk}" --accept-licenses --default-answer
  --confirm-command install copy_only=1 COMMAND_ERROR_IS_FATAL ANY)
file(DOWNLOAD
  "https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip"
  "${work}/glslang.zip" TLS_VERIFY ON
  EXPECTED_HASH SHA256=82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b)
file(ARCHIVE_EXTRACT INPUT "${work}/glslang.zip" DESTINATION "${work}/glslang")
find_program(compiler glslang PATHS "${work}/glslang/bin" NO_DEFAULT_PATH REQUIRED)
find_program(validator spirv-val PATHS "${sdk}/Bin" NO_DEFAULT_PATH REQUIRED)
find_file(layer VkLayer_khronos_validation.json PATHS "${sdk}/Bin" NO_DEFAULT_PATH REQUIRED)
# Supply exact paths to CMake even when another SDK is already on PATH.
file(WRITE "${work}/tools.cmake"
  "set(glslang_tool [==[${compiler}]==] CACHE FILEPATH \"\")\n"
  "set(spirv_val_tool [==[${validator}]==] CACHE FILEPATH \"\")\n")
