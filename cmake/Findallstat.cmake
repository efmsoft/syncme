macro(FindAllStat)
  set(folder ${CMAKE_CURRENT_SOURCE_DIR})

  while(TRUE)
    message("Processing ${folder}")

    if(EXISTS ${folder}/allstat AND EXISTS ${folder}/allstat/AllStat)
      message("Found allstat library: ${folder}/allstat")
      set(ALLSTAT_ROOT ${folder}/allstat)
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

set(USE_ALLSTAT OFF)
set(ALLSTAT_ROOT "")

if(ENABLE_ALLSTAT)
  if(NOT FORCE_FETCH)
    FindAllStat()
  endif()

  if(ALLSTAT_ROOT STREQUAL "")
    set(ALLSTAT_ROOT "${CMAKE_CURRENT_LIST_DIR}/out/allstat")
    if(NOT EXISTS ALLSTAT_ROOT)
      include(FetchContent)
      FetchContent_Declare(allstat
        GIT_REPOSITORY "https://github.com/efmsoft/allstat.git"
        GIT_TAG "master"
        SOURCE_SUBDIR AllStat
        SOURCE_DIR "${ALLSTAT_ROOT}"
      )
      FetchContent_MakeAvailable(allstat)
    endif()
  endif()
endif()

if(NOT ALLSTAT_ROOT STREQUAL "")
  set(ALLSTAT_INCLUDE_DIR "${ALLSTAT_ROOT}/AllStat/include")
  set(USE_ALLSTAT ON)

  add_compile_definitions(USE_ALLSTAT)
  include_directories(syncme PUBLIC
    ${ALLSTAT_INCLUDE_DIR}
  )

  if(TARGET allstat)
    set_target_properties(allstat PROPERTIES FOLDER "Dependencies")
  endif()

  message("Enable allstat usage")
  message("ALLSTAT_INCLUDE_DIR=${ALLSTAT_INCLUDE_DIR}")
else()
  set(USE_ALLSTAT OFF)
  message("Disable allstat usage")
endif()

if(USE_ALLSTAT)
  set(ALLSTAT_LIB allstat)
endif()