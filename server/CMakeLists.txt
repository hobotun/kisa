cmake_minimum_required(VERSION 3.21)
project(kisa-server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt HttpServer — единственный веб-фреймворк (без зоопарка).
find_package(Qt6 REQUIRED COMPONENTS Core Network HttpServer)

add_executable(kisa-server src/main.cpp)
target_link_libraries(kisa-server PRIVATE Qt6::Core Qt6::Network Qt6::HttpServer)

# TODO(следующий шаг): mongocxx — слой хранения.
# find_package(mongocxx REQUIRED) + target_link_libraries(... mongo::mongocxx_shared)
# Пока все ручки возвращают 501 not_implemented, контракт зафиксирован в openapi-стабе.
