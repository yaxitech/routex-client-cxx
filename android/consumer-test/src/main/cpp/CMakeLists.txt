cmake_minimum_required(VERSION 3.22)

project(routex_client_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(routex-client REQUIRED CONFIG)

# probe.cpp is the JNI entry point the instrumented test drives; consumer.cpp is
# the link check, which the static target shares.
add_library(consumer SHARED consumer.cpp probe.cpp)
target_link_libraries(consumer PRIVATE routex-client::routex-client)

# The same check against the static module. Nothing but the module differs, so
# whatever a static consumer has to add beyond it shows up here as a link error.
add_library(consumer_static SHARED consumer.cpp)
target_link_libraries(consumer_static PRIVATE routex-client::routex-client-static)
