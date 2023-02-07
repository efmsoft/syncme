if(WIN32)
  set(OPENSSL_SOURCES ${OPENSSL_INCLUDE_DIR}/openssl/applink.c)
  set(WINLIBS WS2_32.Lib)
endif()

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h") 
add_executable(sockets_test ${SOURCES} ${OPENSSL_SOURCES})

target_compile_definitions(sockets_test PUBLIC 
  USE_LOGME 
  SYNCME_BUILD
)

if(WIN32)
  target_compile_definitions(sockets_test PUBLIC 
    _CRT_SECURE_NO_WARNINGS
  )
endif()

target_link_libraries(sockets_test LINK_PUBLIC gtest_main syncme logme allstat ${OPENSSL_LIBRARIES} ${WINLIBS})

include(GoogleTest)
gtest_discover_tests(sockets_test)

set_target_properties(sockets_test PROPERTIES FOLDER "Tests")  
install(TARGETS sockets_test CONFIGURATIONS ${CMAKE_BUILD_TYPE} RUNTIME DESTINATION ${NF_INSTALL_DIR_BIN})
