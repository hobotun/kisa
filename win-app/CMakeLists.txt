cmake_minimum_required(VERSION 3.21)
project(kisa-win VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../lib
                 ${CMAKE_CURRENT_BINARY_DIR}/kisa-lib)

add_executable(kisa-win WIN32
  src/main.cpp
  src/loginwindow.cpp
  src/mainwindow.cpp
  src/requestswidget.cpp
  src/requestdetailwidget.cpp
)

target_link_libraries(kisa-win PRIVATE kisa-lib Qt6::Widgets)
