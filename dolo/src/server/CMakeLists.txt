dolo_library(dolo_server hub.cpp)  # framing.hpp is header-only
target_link_libraries(dolo_server PUBLIC dolo_core dolo_io dolo_proto)
