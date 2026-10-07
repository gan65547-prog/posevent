cmake_minimum_required(VERSION 3.16)
project(posevent_core LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(zone_core STATIC
    src/region_resolver.cpp
    src/region_router.cpp
    src/zone_dispatch.cpp
    src/posevent_engine.cpp)
target_include_directories(zone_core PUBLIC include)
target_compile_options(zone_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(test_region_core tests/test_region_core.cpp)
target_link_libraries(test_region_core PRIVATE zone_core)

add_executable(test_zone_dispatch tests/test_zone_dispatch.cpp)
target_link_libraries(test_zone_dispatch PRIVATE zone_core)
add_executable(test_posevent_engine tests/test_posevent_engine.cpp)
target_link_libraries(test_posevent_engine PRIVATE zone_core)
add_test(NAME region_core COMMAND test_region_core)
add_test(NAME zone_dispatch COMMAND test_zone_dispatch)
add_test(NAME posevent_engine COMMAND test_posevent_engine)
add_executable(test_zone_matrix tests/test_zone_matrix.cpp)
target_link_libraries(test_zone_matrix PRIVATE zone_core)
add_test(NAME zone_matrix COMMAND test_zone_matrix)
add_executable(test_public_api tests/test_public_api.cpp)
target_link_libraries(test_public_api PRIVATE zone_core)
add_test(NAME public_api COMMAND test_public_api)
