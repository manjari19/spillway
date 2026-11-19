add_executable(spillway_tests
  test_kernels.cpp
  test_spatial_grid.cpp
  test_fluid.cpp
  test_timestep.cpp
  test_terrain.cpp
  test_collision.cpp
  test_level.cpp
  test_world.cpp
  test_marching_squares.cpp
  test_progress.cpp
  test_levels.cpp
)
target_compile_definitions(spillway_tests PRIVATE
  SPILLWAY_LEVEL_DIR="${PROJECT_SOURCE_DIR}/game/levels"
  SPILLWAY_SOLUTION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/solutions")
target_link_libraries(spillway_tests PRIVATE spillway_engine Catch2::Catch2WithMain spillway_warnings)

include(Catch)
catch_discover_tests(spillway_tests)
