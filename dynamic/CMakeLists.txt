get_filename_component(SYNCME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE) 

file(TO_CMAKE_PATH ${SYNCME_ROOT}/lib/source SRCROOT)
file(GLOB_RECURSE SOURCES ${SRCROOT}/*.cpp)

message(STATUS "Syncme dynamic added SYNCME_INCLUDE_DIR=${SYNCME_INCLUDE_DIR}")

add_library(syncmed SHARED ${SOURCES})

target_include_directories(syncmed PRIVATE
  ${SYNCME_INCLUDE_DIR}
  ${LOGME_INCLUDE_DIR}
  ${ALLSTAT_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
)

target_compile_definitions(syncmed PRIVATE _SYNCME_DLL_BUILD_ _WINSOCK_DEPRECATED_NO_WARNINGS)
target_link_libraries(syncmed LINK_PUBLIC ${LOGME_LIBRARIES} ${OPENSSL_LIBRARIES})

if(NOT WIN32)
  target_compile_options(syncmed PRIVATE -fPIC -fvisibility=default)
endif()

if(WIN32)
  target_link_libraries(syncmed PRIVATE Ws2_32)
else()
  target_link_libraries(syncmed LINK_PUBLIC pthread) 
endif()
