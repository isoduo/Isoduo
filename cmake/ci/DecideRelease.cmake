cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/ReleaseDecision.cmake")

set(repo "repos/$ENV{GITHUB_REPOSITORY}")
set(commit "$ENV{GITHUB_SHA}")
# Combine all response pages into one JSON array for each input.
function(fetch_array output endpoint query)
  # @json emits one complete JSON value per line, including quotes for tag names.
  execute_process(COMMAND gh api --paginate "${repo}/${endpoint}" --jq "${query} | @json"
    OUTPUT_VARIABLE rows OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  string(REPLACE "\n" "," values "${rows}")
  set(${output} "[${values}]" PARENT_SCOPE)
endfunction()
fetch_array(tags "tags?per_page=100" ".[].name")
fetch_array(releases "releases?per_page=100" ".[] | {tag_name, draft}")
fetch_array(runs "actions/runs?head_sha=${commit}&event=push&branch=main&per_page=100"
  ".workflow_runs[] | {name, created_at, status, conclusion}")

file(READ "${CMAKE_CURRENT_LIST_DIR}/../../CMakeLists.txt" project_text)
release_decision("$ENV{GITHUB_REF}" "${project_text}" "${commit}" "${tags}" "${releases}" "${runs}")
if(failure)
  message(FATAL_ERROR "${failure}")
endif()
message(STATUS "${title} can be drafted as ${tag} at ${commit}.")
if(DEFINED ENV{GITHUB_OUTPUT})
  file(APPEND "$ENV{GITHUB_OUTPUT}" "tag=${tag}\ntitle=${title}\n")
endif()
if(DEFINED NOTES_PATH)
  file(WRITE "${NOTES_PATH}" "${notes}\n")
endif()
