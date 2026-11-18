add_executable(spillway
  src/main.cpp
)
target_link_libraries(spillway PRIVATE spillway_engine raylib spillway_warnings)
if(APPLE)
  target_link_libraries(spillway PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
