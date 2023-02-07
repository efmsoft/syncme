file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")
file(GLOB_RECURSE HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.h")

add_library(syncme STATIC ${SOURCES} ${HEADERS}) 

target_include_directories(syncme PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LOGME_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
)

target_compile_definitions(syncme PRIVATE SYNCME_BUILD)

if(USE_ALLSTAT)
  target_compile_definitions(syncme PUBLIC USE_ALLSTAT)
endif()

if(WIN32)
  target_compile_definitions(syncme PUBLIC 
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _CRT_SECURE_NO_WARNINGS
  )
endif()

