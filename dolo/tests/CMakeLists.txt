find_package(GTest REQUIRED)  # Debian/Raspberry Pi OS: sudo apt install libgtest-dev
include(GoogleTest)

add_executable(dolo_tests
  adsb/adsb_test.cpp
  app/config_test.cpp
  gnss/protocol_test.cpp gnss/receivers_test.cpp
  server/server_test.cpp)
target_include_directories(dolo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dolo_tests PRIVATE
  dolo_config dolo_adsb dolo_gnss dolo_server dolo_io dolo_core dolo_proto GTest::gtest_main)

# One ctest entry per TEST(), so `ctest -R Ubx` or a single failing case can be run on its own.
gtest_discover_tests(dolo_tests)
