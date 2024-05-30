if(WIN32)
  set(WINLIBS WS2_32.Lib)
endif()

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h") 
add_executable(mapping ${TESTMAIN} ${SOURCES} ${OPENSSL_SOURCES})

target_compile_definitions(mapping PUBLIC SYNCME_BUILD)

target_compile_definitions(mapping PUBLIC 
  _SINCME_STATIC_BUILD_
)

if(WIN32)
  target_compile_definitions(mapping PUBLIC 
    _CRT_SECURE_NO_WARNINGS
  )
endif()

target_link_libraries(mapping LINK_PUBLIC 
  gtest_main syncme  
  ${LOGME_LIB} 
  ${ALLSTAT_LIB} 
  ${WINLIBS}
)

include(GoogleTest)
gtest_discover_tests(mapping)

set_target_properties(mapping PROPERTIES FOLDER "Tests")  
install(TARGETS mapping CONFIGURATIONS ${CMAKE_BUILD_TYPE} RUNTIME DESTINATION ${NF_INSTALL_DIR_BIN})
