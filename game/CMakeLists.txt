add_executable(spillway
  src/main.cpp
  src/app.cpp
  src/assets.cpp
  src/renderer.cpp
  src/terrain_view.cpp
  src/effects.cpp
  src/widgets.cpp
  src/menu_scene.cpp
  src/gameplay_scene.cpp
)
target_link_libraries(spillway PRIVATE spillway_engine raylib spillway_warnings)
target_compile_definitions(spillway PRIVATE SPILLWAY_SOURCE_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(APPLE)
  target_link_libraries(spillway PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# Ship levels, shaders and fonts next to the executable.
add_custom_command(TARGET spillway POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/levels $<TARGET_FILE_DIR:spillway>/levels
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:spillway>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:spillway>/assets)
