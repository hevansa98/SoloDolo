cmake_minimum_required(VERSION 3.18)
project(dolo VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Executables land in the build root (build/dolo, build/dolo_tests) wherever their CMakeLists lives; deploy/install.sh relies on it.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Debian/Raspberry Pi OS: sudo apt install cmake g++ protobuf-compiler libprotobuf-dev \
#                                           libyaml-cpp-dev nlohmann-json3-dev libgtest-dev
find_package(Protobuf REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

# Settings every Dolo library shares: the include root ("gnss/state.hpp" style paths), warnings, the version string.
add_library(dolo_options INTERFACE)
target_include_directories(dolo_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(dolo_options INTERFACE -Wall -Wextra)
target_compile_definitions(dolo_options INTERFACE DOLO_VERSION="${PROJECT_VERSION}")

# dolo_library(<name> <sources...>): a static library of one component, sources relative to the calling directory.
function(dolo_library name)
  add_library(${name} STATIC ${ARGN})
  target_link_libraries(${name} PUBLIC dolo_options)
endfunction()

# The wire contract is shared with Solo (the iOS app) and lives outside this directory.
set(SOLODOLO_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../proto/solodolo/v1)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/gen)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${SOLODOLO_PROTO_DIR}/solodolo.proto
                      PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
add_library(dolo_proto STATIC ${PROTO_SRCS})  # generated code: no project warning flags
target_include_directories(dolo_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_link_libraries(dolo_proto PUBLIC protobuf::libprotobuf)

# Components, lowest layer first. Link dependencies mirror the include rule documented in ARCHITECTURE.md:
#   core <- io <- adsb / gnss / server <- app
add_subdirectory(src/core)    # dolo_core:   event loop
add_subdirectory(src/io)      # dolo_io:     sockets, serial port
add_subdirectory(src/adsb)    # dolo_adsb:   SBS parsing, aircraft store
add_subdirectory(src/gnss)    # dolo_gnss:   GNSS receivers
add_subdirectory(src/server)  # dolo_server: client hub
add_subdirectory(src/app)     # dolo_config + the dolo executable

enable_testing()
add_subdirectory(tests)
