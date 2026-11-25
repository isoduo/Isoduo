cmake_minimum_required(VERSION 3.25)
execute_process(COMMAND gh api --method POST "repos/$ENV{GITHUB_REPOSITORY}/releases"
  -f "tag_name=$ENV{TAG}" -f "name=$ENV{TITLE}" -f "target_commitish=$ENV{GITHUB_SHA}"
  -F "body=@${NOTES_PATH}" -F draft=true -F prerelease=false -F generate_release_notes=true
  --jq ".html_url"
  OUTPUT_VARIABLE url OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
file(APPEND "$ENV{GITHUB_STEP_SUMMARY}" "Draft release for $ENV{TITLE}: ${url}\n")
