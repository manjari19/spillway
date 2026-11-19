# The engine is pure C++20 with no graphics dependency, so it can be used by
# the game, the headless benchmark and the tests alike.
add_library(spillway_engine STATIC
  src/spatial_grid.cpp
  src/fluid.cpp
  src/timestep.cpp
  src/simulation.cpp
  src/terrain.cpp
  src/level.cpp
  src/world.cpp
  src/marching_squares.cpp
  src/json.cpp
  src/progress.cpp
)
target_include_directories(spillway_engine PUBLIC include)
target_compile_features(spillway_engine PUBLIC cxx_std_20)
target_link_libraries(spillway_engine PRIVATE spillway_warnings)
