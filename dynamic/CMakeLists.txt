get_filename_component(SYNCME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE) 

file(TO_CMAKE_PATH ${SYNCME_ROOT}/lib/include SYNCME_LIB_INCLUDE)
file(TO_CMAKE_PATH ${SYNCME_ROOT}/lib/source SYNCME_LIB_SOURCE)

file(GLOB_RECURSE SOURCES ${SYNCME_LIB_SOURCE}/*.cpp)
file(GLOB_RECURSE HEADERS ${SYNCME_LIB_INCLUDE}/*.h)

message(STATUS "Syncme dynamic added SYNCME_INCLUDE_DIR=${SYNCME_INCLUDE_DIR}")

add_library(syncmed SHARED ${SOURCES} ${HEADERS} ${SYNCME_VERSION_RC})

target_include_directories(syncmed PRIVATE
  ${SYNCME_INCLUDE_DIR}
  ${LOGME_INCLUDE_DIR}
  ${ALLSTAT_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
)

target_compile_definitions(syncmed PRIVATE _SYNCME_DLL_BUILD_ _WINSOCK_DEPRECATED_NO_WARNINGS)
target_link_libraries(syncmed LINK_PUBLIC ${LOGME_LIBRARIES} ${OPENSSL_LIBRARIES} ${LOGME_LIB} ${ALLSTAT_LIB})

if(NOT WIN32)
  target_compile_options(syncmed PRIVATE -fPIC -fvisibility=default)
endif()

if(WIN32)
  target_link_libraries(syncmed PRIVATE Ws2_32)
else()
  target_link_libraries(syncmed LINK_PUBLIC pthread) 
endif()
