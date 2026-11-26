cmake_minimum_required(VERSION 3.25)
project(IdentifierNamingFixture LANGUAGES NONE)

function(isoduo_target target)
endfunction()
enable_testing()
include("${SOURCE_DIR}/tests/register_identifier_naming.cmake")
