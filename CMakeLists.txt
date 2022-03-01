cmake_minimum_required(VERSION 3.1)
project(QtWiznetCanBusPlugin C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

find_package(Qt5Core REQUIRED)
find_package(Qt5SerialBus REQUIRED)
find_package(Qt5Network REQUIRED)

include_directories(${Qt5Core_INCLUDE_DIRS} ${Qt5SerialBus_INCLUDE_DIRS}
    ${Qt5Network_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
    ./src/bsp
    ./src/common
    ./src/hw
    )

add_library(${PROJECT_NAME} SHARED
  QtWiznetCanBusPlugin.cpp
  WiznetCanBackend.cpp
  ./src/bsp/bsp.c
  ./src/common/qbuffer.c
  ./src/hw/cmd_can.c
  )

target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::SerialBus Qt5::Network)
