cmake_minimum_required(VERSION 3.20)
project(phsp_tools VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(ROOT 6.34 REQUIRED COMPONENTS RIO Tree TreePlayer Hist Graf Gpad ROOTDataFrame Matrix)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)
include(CTest)
include(GNUInstallDirs)

add_subdirectory(core)
add_subdirectory(app)

message(STATUS "PHSP Tools ${PROJECT_VERSION}; ROOT ${ROOT_VERSION}; C++${CMAKE_CXX_STANDARD}")
