add_executable(spillway_tests
  test_kernels.cpp
  test_spatial_grid.cpp
  test_fluid.cpp
  test_timestep.cpp
  test_terrain.cpp
  test_collision.cpp
  test_level.cpp
  test_world.cpp
)
target_compile_definitions(spillway_tests PRIVATE SPILLWAY_LEVEL_DIR="${PROJECT_SOURCE_DIR}/game/levels")
target_link_libraries(spillway_tests PRIVATE spillway_engine Catch2::Catch2WithMain spillway_warnings)

include(Catch)
catch_discover_tests(spillway_tests)
