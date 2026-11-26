cmake_minimum_required(VERSION 3.25)

execute_process(COMMAND sudo apt-get update COMMAND_ERROR_IS_FATAL ANY)
if(ISODUO_INSTALL_RENDER_TEST_PACKAGES)
  set(render_test_packages libegl-mesa0 libgl1-mesa-dri mesa-vulkan-drivers xauth xvfb)
endif()
execute_process(
  COMMAND sudo apt-get install -y --no-install-recommends
    pkg-config libegl-dev libx11-dev libx11-xcb-dev libxcb1-dev
    libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev
    libfontconfig-dev libglib2.0-dev libpango1.0-dev libgtk-3-dev
    ${render_test_packages}
  COMMAND_ERROR_IS_FATAL ANY)
