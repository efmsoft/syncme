if(WIN32)
  set(OPENSSL_SOURCES ${OPENSSL_INCLUDE_DIR}/openssl/applink.c)
  set(WINLIBS WS2_32.Lib)
endif() 

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c*" "*.h") 
add_executable(Socket ${SOURCES} ${OPENSSL_SOURCES})

target_link_libraries(Socket LINK_PUBLIC syncme ${LOGME_LIB} ${ALLSTAT_LIB} ${OPENSSL_LIBRARIES} ${WINLIBS})

set_target_properties(Socket PROPERTIES FOLDER "Examples")  
