cmake_minimum_required(VERSION 3.25)

set(packages clang-tidy clang-tools)
if(DESKTOP)
  list(APPEND packages
    pkg-config libegl-dev libx11-dev libx11-xcb-dev libxcb1-dev
    libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev
    libfontconfig-dev libglib2.0-dev libpango1.0-dev libgtk-3-dev)
endif()
execute_process(COMMAND sudo apt-get update COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND sudo apt-get install -y --no-install-recommends ${packages}
  COMMAND_ERROR_IS_FATAL ANY)
