dolo_library(dolo_adsb aircraft.cpp sbs.cpp)  # geo.hpp is header-only
target_link_libraries(dolo_adsb PUBLIC dolo_core dolo_proto)
