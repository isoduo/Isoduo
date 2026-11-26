cmake_minimum_required(VERSION 3.25)

# Run the driver directly so a missing renderer (return code 77) fails this job.
foreach(backend IN ITEMS opengl vulkan)
  # Closing the capability probe drops Xvfb's last client. Keep the server
  # from resetting while the first sweep row opens its X11 connection.
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=WAYLAND_DISPLAY XDG_SESSION_TYPE=x11 LIBGL_ALWAYS_SOFTWARE=1
      xvfb-run -a -s "-screen 0 1280x1024x24 -noreset"
      "${BUILD_DIR}/isoduo_linux_desktop_native" x11 fault-sweep --render-backend ${backend}
    COMMAND_ERROR_IS_FATAL ANY TIMEOUT 240)
endforeach()
