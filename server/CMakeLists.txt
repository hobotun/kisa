cmake_minimum_required(VERSION 3.21)
project(kisa-server VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt HttpServer — единственный веб-фреймворк (без зоопарка).
find_package(Qt6 REQUIRED COMPONENTS Core Network HttpServer)
# Mongo C++ driver (собирается в Dockerfile из исходников).
find_package(mongocxx REQUIRED)
# argon2id для паролей.
find_library(ARGON2_LIB argon2 REQUIRED)

add_executable(kisa-server
  src/main.cpp
  src/app.cpp
  src/auth.cpp
  src/meta.cpp
)
target_include_directories(kisa-server PRIVATE src)
target_link_libraries(kisa-server PRIVATE
  Qt6::Core Qt6::Network Qt6::HttpServer
  mongo::mongocxx_shared
  ${ARGON2_LIB}
)
