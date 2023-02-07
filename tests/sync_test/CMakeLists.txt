file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h") 
add_executable(sync_test ${SOURCES})

target_link_libraries(sync_test LINK_PUBLIC gtest_main syncme  ${LOGME_LIB} ${ALLSTAT_LIB})

include(GoogleTest)
gtest_discover_tests(sync_test)

set_target_properties(sync_test PROPERTIES FOLDER "Tests")  
install(TARGETS sync_test CONFIGURATIONS ${CMAKE_BUILD_TYPE} RUNTIME DESTINATION ${NF_INSTALL_DIR_BIN})
