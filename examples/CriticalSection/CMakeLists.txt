file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h" "*.md") 
add_executable(CriticalSection ${SOURCES})

target_link_libraries(CriticalSection LINK_PUBLIC syncme ${LOGME_LIB} ${ALLSTAT_LIB} ${OPENSSL_LIBRARIES})

set_target_properties(CriticalSection PROPERTIES FOLDER "Examples")  
