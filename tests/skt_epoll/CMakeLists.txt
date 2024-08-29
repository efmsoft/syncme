project(skt_epoll)

if(WIN32)
  set(OPENSSL_SOURCES ${OPENSSL_INCLUDE_DIR}/openssl/applink.c)
  set(WINLIBS WS2_32.Lib)
endif()

set(TESTMAIN ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp)

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h") 
add_executable(${PROJECT_NAME} ${TESTMAIN} ${SOURCES} ${OPENSSL_SOURCES})

target_compile_definitions(${PROJECT_NAME} PUBLIC SYNCME_BUILD)

target_compile_definitions(${PROJECT_NAME} PUBLIC 
  _SINCME_STATIC_BUILD_
)

if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PUBLIC 
    _CRT_SECURE_NO_WARNINGS
    _SINCME_STATIC_BUILD_
  )
endif()

target_link_libraries(${PROJECT_NAME} LINK_PUBLIC 
  gtest_main syncme  
  ${LOGME_LIB} 
  ${ALLSTAT_LIB} 
  ${OPENSSL_LIBRARIES} 
  ${WINLIBS}
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "Tests")  
install(TARGETS ${PROJECT_NAME} CONFIGURATIONS ${CMAKE_BUILD_TYPE} RUNTIME DESTINATION ${NF_INSTALL_DIR_BIN})
