cmake_minimum_required(VERSION 3.0.0)
project(syncme VERSION 1.1.0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) 

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

cmake_policy(SET CMP0077 NEW)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE) 

if (CMAKE_BUILD_TYPE STREQUAL "")
  message(STATUS "  Diag: Build type was unspecified, set to Release")
  set(CMAKE_BUILD_TYPE Release)
else ()
  message(STATUS "  Diag: Build type specified as '${CMAKE_BUILD_TYPE}'")
endif ()

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
  set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "_DEBUG")
endif ()

set(ENABLE_ALLSTAT OFF)
set(ENABLE_LOGME OFF)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})

find_package(allstat MODULE)
find_package(logme MODULE)
find_package(OpenSSL REQUIRED) 

enable_testing()
add_subdirectory(lib ../out/Static)
add_subdirectory(tests ../out/Tests)
