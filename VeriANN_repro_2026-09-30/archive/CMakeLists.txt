cmake_minimum_required(VERSION 3.12)
project(veriann_ivf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(veriann main.cpp)
target_compile_options(veriann PRIVATE -O3 -Wall -Wextra -Wpedantic)

