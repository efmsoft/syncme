file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")
file(GLOB_RECURSE HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.h")

add_library(syncme STATIC ${SOURCES} ${HEADERS}) 

if(USE_BORINGSSL)
  set(_ssl_include_dir ${BORINGSSL_INCLUDE_DIR})
else()
  set(_ssl_include_dir ${OPENSSL_INCLUDE_DIR})
endif()


target_include_directories(syncme PUBLIC
  ${ALLSTAT_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LOGME_INCLUDE_DIR}
  ${_ssl_include_dir}
)

target_compile_definitions(syncme PRIVATE SYNCME_BUILD)
target_compile_definitions(syncme PRIVATE _SINCME_STATIC_BUILD_)

if(USE_ALLSTAT)
  target_compile_definitions(syncme PUBLIC USE_ALLSTAT)
endif()

if(WIN32)
  target_compile_definitions(syncme PUBLIC 
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _CRT_SECURE_NO_WARNINGS
  )
endif()

