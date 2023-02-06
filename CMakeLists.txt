cmake_minimum_required(VERSION 3.0.0)
project(syncme VERSION 1.1.0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) 

if (CMAKE_BUILD_TYPE STREQUAL "")
  message(STATUS "  Diag: Build type was unspecified, set to Release")
  set(CMAKE_BUILD_TYPE Release)
else ()
  message(STATUS "  Diag: Build type specified as '${CMAKE_BUILD_TYPE}'")
endif ()

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
  set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "_DEBUG")
endif ()


set(ENABLE_LOGME ON)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})
find_package(logme MODULE)

find_package(OpenSSL REQUIRED) 

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/lib)

add_subdirectory(lib ../out/Static)
#add_subdirectory(tests ../out/Tests)

