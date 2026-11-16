# Configuration is the one place that knows about the YAML file; it depends on gnss for GnssSettings.
dolo_library(dolo_config config.cpp)
target_link_libraries(dolo_config PUBLIC dolo_gnss dolo_io PRIVATE yaml-cpp)

# main.cpp only wires the components together.
add_executable(dolo main.cpp)
target_link_libraries(dolo PRIVATE dolo_config dolo_adsb dolo_gnss dolo_server dolo_io dolo_core)

install(TARGETS dolo RUNTIME DESTINATION bin)
