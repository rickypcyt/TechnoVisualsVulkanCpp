
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(${CMAKE_CURRENT_LIST_DIR}/KFRTargets.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/KFRConfigVersion.cmake)

include(CMakeFindDependencyMacro)

set(KFR_AUDIO_FLAC )
set(KFR_AUDIO_ALAC )

if (KFR_AUDIO_FLAC)
    # Search for FLAC libraries in both release and debug install dirs
    find_library(FLAC_LIBRARY_REL NAMES FLAC PATHS ${PACKAGE_PREFIX_DIR}/lib NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FLAC_LIBRARY_DBG NAMES FLAC PATHS ${PACKAGE_PREFIX_DIR}/lib/debug NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    target_link_libraries(kfr_audio INTERFACE 
        $<$<CONFIG:Debug>:${FLAC_LIBRARY_DBG}>
        $<$<CONFIG:Release>:${FLAC_LIBRARY_REL}>
    )

    # Search for OGG libraries in both release and debug install dirs
    find_library(OGG_LIBRARY_REL NAMES ogg PATHS ${PACKAGE_PREFIX_DIR}/lib NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(OGG_LIBRARY_DBG NAMES ogg PATHS ${PACKAGE_PREFIX_DIR}/lib/debug NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    target_link_libraries(kfr_audio INTERFACE 
        $<$<CONFIG:Debug>:${OGG_LIBRARY_DBG}>
        $<$<CONFIG:Release>:${OGG_LIBRARY_REL}>
    )
endif ()

if (KFR_AUDIO_ALAC)
    # Search for ALAC libraries in both release and debug install dirs
    find_library(ALAC_LIBRARY_REL NAMES libalac PATHS ${PACKAGE_PREFIX_DIR}/lib NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(ALAC_LIBRARY_DBG NAMES libalac PATHS ${PACKAGE_PREFIX_DIR}/lib/debug NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    
    target_link_libraries(kfr_audio INTERFACE 
        $<$<CONFIG:Debug>:${ALAC_LIBRARY_DBG}>
        $<$<CONFIG:Release>:${ALAC_LIBRARY_REL}>
    )
endif ()
