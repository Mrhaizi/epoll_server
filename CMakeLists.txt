cmake_minimum_required(VERSION 3.17)
project(epoll_test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
add_executable(${PROJECT_NAME} main.cpp)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror=return-type)


