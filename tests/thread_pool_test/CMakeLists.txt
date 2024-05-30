if(WIN32)
  set(WINLIBS WS2_32.Lib)
endif()

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h") 
add_executable(thread_pool_test ${SOURCES})

target_link_libraries(thread_pool_test LINK_PUBLIC gtest_main syncme ${LOGME_LIB} ${ALLSTAT_LIB} ${WINLIBS})

target_compile_definitions(thread_pool_test PUBLIC 
  _SINCME_STATIC_BUILD_
)

include(GoogleTest)
gtest_discover_tests(thread_pool_test)

set_target_properties(thread_pool_test PROPERTIES FOLDER "Tests")  
install(TARGETS thread_pool_test CONFIGURATIONS ${CMAKE_BUILD_TYPE} RUNTIME DESTINATION ${NF_INSTALL_DIR_BIN})
