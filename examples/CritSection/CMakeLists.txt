file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h" "*.md") 
add_executable(CritSection ${SOURCES})

target_link_libraries(CritSection LINK_PUBLIC syncme ${LOGME_LIB} ${ALLSTAT_LIB} ${OPENSSL_LIBRARIES})

set_target_properties(CritSection PROPERTIES FOLDER "Examples")  
