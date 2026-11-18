add_executable(spillway
  src/main.cpp
  src/gameplay.cpp
  src/ui.cpp
)
target_link_libraries(spillway PRIVATE spillway_engine raylib spillway_warnings)
target_compile_definitions(spillway PRIVATE SPILLWAY_SOURCE_LEVEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/levels")
if(APPLE)
  target_link_libraries(spillway PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# Ship the level files next to the executable.
add_custom_command(TARGET spillway POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/levels $<TARGET_FILE_DIR:spillway>/levels)
