if(WIN32)
  set(OPENSSL_SOURCES ${OPENSSL_INCLUDE_DIR}/openssl/applink.c)
  set(WINLIBS WS2_32.Lib)
endif()

set(TESTMAIN ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp)

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h") 
add_executable(sockets_lt_et ${TESTMAIN} ${SOURCES} ${OPENSSL_SOURCES})

target_compile_definitions(sockets_lt_et PUBLIC SYNCME_BUILD)

if(WIN32)
  target_compile_definitions(sockets_lt_et PUBLIC 
    _CRT_SECURE_NO_WARNINGS
  )
endif()

target_link_libraries(sockets_lt_et LINK_PUBLIC 
  gtest_main syncme  
  ${LOGME_LIB} 
  ${ALLSTAT_LIB} 
  ${OPENSSL_LIBRARIES} 
  ${WINLIBS}
)

include(GoogleTest)
gtest_discover_tests(sockets_lt_et)

set_target_properties(sockets_lt_et PROPERTIES FOLDER "Tests")  
install(TARGETS sockets_lt_et CONFIGURATIONS ${CMAKE_BUILD_TYPE} RUNTIME DESTINATION ${NF_INSTALL_DIR_BIN})
