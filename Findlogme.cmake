macro(FindLogme)
  set(folder ${CMAKE_CURRENT_SOURCE_DIR})

  while(TRUE)
    message("Processing ${folder}")

    if(EXISTS ${folder}/logme AND EXISTS ${folder}/logme/logme)
      message("Found logme library: ${folder}/logme")
      set(LOGME_ROOT ${folder})
      break()
    endif()

    cmake_path(HAS_PARENT_PATH folder has_parent)
    if(NOT has_parent)
      break()
    endif()

    cmake_path(GET folder PARENT_PATH folder)
  endwhile()
endmacro()

FindLogme()

set(USE_LOGME OFF)
if(NOT LOGME_ROOT STREQUAL "")
  if(ENABLE_LOGME)
    set(USE_LOGME ON)
    add_definitions(-DUSE_LOGME)
    set(LOGME_INCLUDE_DIR "${LOGME_ROOT}/logme/logme/include")

    message("Enable logme usage")
    message("LOGME_INCLUDE_DIR=${LOGME_INCLUDE_DIR}")
  endif()
endif()

if(NOT USE_LOGME)
  message("Disable logme usage")
endif()