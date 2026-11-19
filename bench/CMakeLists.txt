# Headless benchmark: engine only, no raylib.
add_executable(spillway_bench bench.cpp)
target_link_libraries(spillway_bench PRIVATE spillway_engine spillway_warnings)
target_compile_definitions(spillway_bench PRIVATE
  SPILLWAY_BENCH_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/results"
  SPILLWAY_BUILD_TYPE="$<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},$<CONFIG>>")
