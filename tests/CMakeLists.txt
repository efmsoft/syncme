if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

cmake_policy(SET CMP0077 NEW)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_disable_pthreads OFF CACHE BOOL "" FORCE) 

message("CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")
message("CMAKE_CXX_COMPILER_VERSION = ${CMAKE_CXX_COMPILER_VERSION}")

include_directories(${CMAKE_SOURCE_DIR}/tests)

set(GTEST true)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "5.0.0")
    message("WARNING: Some tests are disabled due to version of GCC compiler !!!")
    set(GTEST false)
  endif()
endif()

if(GTEST)
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

if(USE_LOGME)
  add_subdirectory(sockets_test)
  add_subdirectory(sockets_lt_et)
  if(NOT WIN32)
    add_subdirectory(skt_epoll)
  endif()
endif()

add_subdirectory(sync_test)
add_subdirectory(thread_pool_test)

if(WIN32)
  add_subdirectory(mapping)
endif()

set_target_properties(gmock PROPERTIES FOLDER "Tests/gtest")
set_target_properties(gmock_main PROPERTIES FOLDER "Tests/gtest")
set_target_properties(gtest PROPERTIES FOLDER "Tests/gtest")
set_target_properties(gtest_main PROPERTIES FOLDER "Tests/gtest")

endif()
