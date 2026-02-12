# Include this file to your project's CMakeList.txt to initialize
# SYNCME_INCLUDE_DIR and SYNCME_LIBRARIES variables and add syncme
# subproject
#
# It is clear that this file does not try to search for the location 
# of the library, but knows it exactly because it is located in the 
# cmake subdirectory. We implemented this approach to connect all our 
# libraries in a uniform way. Both from the root files of the 
# repositories, and their use by external projects. In each case, it 
# is enough to add the path to the directory and start the search
#
# list(APPEND CMAKE_MODULE_PATH <location_of_lib_in_your_project>/cmake) 
# find_package(syncme MODULE) 

get_filename_component(SYNCME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE) 

set(SYNCME_INCLUDE_DIR ${SYNCME_ROOT}/lib/include)

# SSL backend selection is controlled by the parent project.
# When USE_BORINGSSL is enabled, the parent must provide BORINGSSL_INCLUDE_DIR and BORINGSSL_LIBRARIES.
if(USE_BORINGSSL)
  add_compile_definitions(USE_BORINGSSL)
  if(NOT DEFINED BORINGSSL_INCLUDE_DIR)
    message(FATAL_ERROR "USE_BORINGSSL is ON but BORINGSSL_INCLUDE_DIR is not set")
  endif()
  if(NOT DEFINED BORINGSSL_LIBRARIES)
    message(FATAL_ERROR "USE_BORINGSSL is ON but BORINGSSL_LIBRARIES is not set")
  endif()

  # Reuse existing OPENSSL_* variables expected by syncme's build scripts.
  set(OPENSSL_INCLUDE_DIR ${BORINGSSL_INCLUDE_DIR})
  set(OPENSSL_LIBRARIES ${BORINGSSL_LIBRARIES})
endif()


if(${USE_SYNCME_SHARED})
  add_subdirectory(${SYNCME_ROOT}/dynamic)
  set(SYNCME_LIBRARIES syncmed)
else()
  add_subdirectory(${SYNCME_ROOT}/lib)
  set(SYNCME_LIBRARIES syncme)
endif()
