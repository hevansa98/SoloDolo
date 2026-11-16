dolo_library(dolo_gnss
  factory.cpp gpsd.cpp gt_u7.cpp max_m8q.cpp
  nmea.cpp state.cpp stream.cpp
  ubx.cpp uart_receiver.cpp)
target_link_libraries(dolo_gnss PUBLIC dolo_core dolo_io dolo_proto nlohmann_json::nlohmann_json)
