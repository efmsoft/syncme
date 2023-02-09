macro(FindLogme)
  set(folder ${CMAKE_CURRENT_SOURCE_DIR})

  while(TRUE)
    message("Processing ${folder}")

    if(EXISTS ${folder}/logme AND EXISTS ${folder}/logme/logme)
      message("Found logme library: ${folder}/logme")
      set(LOGME_ROOT ${folder}/logme)
      break()
    endif()

    cmake_path(HAS_PARENT_PATH folder has_parent)
    if(NOT has_parent)
      break()
    endif()

    set(previous ${folder})
    cmake_path(GET folder PARENT_PATH folder)

    if(folder STREQUAL previous)
      break()
    endif()
  endwhile()
endmacro()

set(USE_LOGME OFF)
set(LOGME_ROOT "")

if(ENABLE_LOGME)
  if(NOT FORCE_FETCH)
    FindLogme()
  endif()

  if(LOGME_ROOT STREQUAL "")
    set(LOGME_ROOT "${CMAKE_CURRENT_LIST_DIR}/out/logme")
    if(NOT EXISTS LOGME_ROOT)
      include(FetchContent)
      FetchContent_Declare(logme
        GIT_REPOSITORY "https://github.com/efmsoft/logme.git"
        GIT_TAG "main"
        SOURCE_SUBDIR logme
        SOURCE_DIR "${LOGME_ROOT}"
      )
      FetchContent_MakeAvailable(logme)
    endif()
  endif()
endif()

if(NOT LOGME_ROOT STREQUAL "")
  set(USE_LOGME ON)
  set(LOGME_INCLUDE_DIR "${LOGME_ROOT}/logme/include")

  add_compile_definitions(USE_LOGME)
  include_directories(syncme PUBLIC
    ${LOGME_INCLUDE_DIR}
  )

  if(TARGET logme)
    set_target_properties(logme PROPERTIES FOLDER "Dependencies")
  endif()

  message("Enable logme usage")
  message("LOGME_INCLUDE_DIR=${LOGME_INCLUDE_DIR}")
else()
  set(USE_LOGME OFF)
  message("Disable logme usage")
endif()

if(USE_LOGME)
  set(LOGME_LIB logme)
endif()