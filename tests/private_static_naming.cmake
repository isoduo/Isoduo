# clang-tidy classifies static fields without regard to access. Ask Clang's
# AST which declarations are private so public static constants can stay unsuffixed.
function(find_private_static_naming_violations output source_filter)
  string(CONCAT query
    "match varDecl(hasDeclContext(cxxRecordDecl()), isStaticStorageClass(), isPrivate(), "
    "isExpansionInFileMatching(\"${source_filter}\"), "
    "unless(anyOf(isExpansionInSystemHeader(), isExpansionInFileMatching(\"[/\\\\]third_party[/\\\\]\"), "
    "matchesName(\"_$\")))).bind(\"private static member requires a trailing underscore\")")
  execute_process(
    COMMAND "${CLANG_QUERY}"
      -c "set traversal IgnoreUnlessSpelledInSource"
      -c "set bind-root false"
      -c "${query}"
      ${ARGN}
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE diagnostics
    COMMAND_ERROR_IS_FATAL ANY)
  if(diagnostics MATCHES "(^|\n)Match #[0-9]+:")
    set(${output} "${diagnostics}" PARENT_SCOPE)
  else()
    set(${output} "" PARENT_SCOPE)
  endif()
endfunction()
