function(register_script_guard checker)
  add_test(script_guard "${CMAKE_COMMAND}" ${ARGN} -P "${checker}")
  # A forbidden filename may contain the skip message inside a fatal diagnostic.
  set_tests_properties(script_guard PROPERTIES SKIP_REGULAR_EXPRESSION
    "^-- Skipping tracked script check: (source archive has no Git index|Git executable unavailable)[\r\n]*$")
endfunction()
