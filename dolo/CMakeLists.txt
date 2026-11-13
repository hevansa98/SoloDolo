cmake_minimum_required(VERSION 3.18)
project(dolo VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Debian/Raspberry Pi OS: sudo apt install cmake g++ protobuf-compiler libprotobuf-dev \
#                                           libyaml-cpp-dev nlohmann-json3-dev
find_package(Protobuf REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

# The wire contract is shared with Solo (the iOS app) and lives outside this directory.
set(SOLODOLO_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../proto/solodolo/v1)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/gen)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${SOLODOLO_PROTO_DIR}/solodolo.proto
                      PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)

add_library(dolo_core STATIC
  src/aircraft.cpp src/config.cpp src/event_loop.cpp src/gnss.cpp
  src/hub.cpp src/net.cpp src/sbs.cpp src/serial.cpp ${PROTO_SRCS})
target_include_directories(dolo_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_definitions(dolo_core PUBLIC DOLO_VERSION="${PROJECT_VERSION}")
target_compile_options(dolo_core PRIVATE -Wall -Wextra)
target_link_libraries(dolo_core PUBLIC protobuf::libprotobuf yaml-cpp nlohmann_json::nlohmann_json)

add_executable(dolo src/main.cpp)
target_link_libraries(dolo PRIVATE dolo_core)

enable_testing()
add_executable(dolo_tests tests/tests.cpp)
target_link_libraries(dolo_tests PRIVATE dolo_core)
add_test(NAME dolo_tests COMMAND dolo_tests)

install(TARGETS dolo RUNTIME DESTINATION bin)
