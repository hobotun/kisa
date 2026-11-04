cmake_minimum_required(VERSION 3.21)
project(kisa-lib VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(kisa-lib STATIC
  src/dto.cpp
  src/tenantsession.cpp
  src/kisaclient.cpp
)

target_include_directories(kisa-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kisa-lib PUBLIC Qt6::Core Qt6::Network)
