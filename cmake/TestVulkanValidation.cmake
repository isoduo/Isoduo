cmake_minimum_required(VERSION 3.25)
# A loader may ignore an unavailable layer requested via VK_INSTANCE_LAYERS.
# Require its insertion in addition to a successful offscreen comparison.
set(ENV{VK_INSTANCE_LAYERS} VK_LAYER_KHRONOS_validation)
set(ENV{VK_LOADER_DEBUG} layer)
execute_process(COMMAND "${TEST_PROGRAM}" --offscreen RESULT_VARIABLE result
  OUTPUT_VARIABLE log ERROR_VARIABLE log)
message("${log}")
if(NOT result STREQUAL "0" OR NOT log MATCHES "Insert instance layer \"VK_LAYER_KHRONOS_validation\""
   OR log MATCHES "Validation Error|VUID-|\\[Error\\]|ERROR / SPEC")
  message(FATAL_ERROR "Offscreen comparison requires an active validation layer and zero errors")
endif()
