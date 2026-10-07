cmake_minimum_required(VERSION 3.16)
project(posevent VERSION 0.1.0 LANGUAGES C CXX)

enable_testing()

add_executable(posevent main.c)
target_compile_options(posevent PRIVATE -Wall -Wextra -Wpedantic)

add_subdirectory(cpp)
add_subdirectory(pipeline)
