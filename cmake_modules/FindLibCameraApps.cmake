# - Try to find LIBCAMERAAPPS Library
# Once done this will define
#
#  LIBCAMERAAPPS_FOUND - system has LIBCAMERAAPPS
#  LIBCAMERAAPPS_APPS - Link these to use Apps
#  LIBCAMERAAPPS_ENCODERS - Link these to use Encoders
#  LIBCAMERAAPPS_IMAGES - Link these to use Images
#  LIBCAMERAAPPS_OUTPUTS - Link these to use Outputs
#  LIBCAMERAAPPS_PREVIEW - Link these to use Preview
#  LIBCAMERAAPPS_POST - Link these to use Post Processing Stages

# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if (LIBCAMERAAPPS_APPS AND LIBCAMERAAPPS_INCLUDE_DIR)

  # in cache already
  set(LIBCAMERAAPPS_FOUND TRUE)
  message(STATUS "Found LIBCAMERAAPPS: ${LIBCAMERAAPPS_APPS}")

else (LIBCAMERAAPPS_APPS AND LIBCAMERAAPPS_INCLUDE_DIR)

    find_library(LIBCAMERAAPPS_APPS NAMES camera_app
      PATHS
      ${_obLinkDir}
      ${GNUWIN32_DIR}/lib
    )

    find_library(LIBCAMERAAPPS_ENCODERS NAMES encoders
      PATHS
      ${_obLinkDir}
      ${GNUWIN32_DIR}/lib
    )

    find_library(LIBCAMERAAPPS_IMAGES NAMES images
      PATHS
      ${_obLinkDir}
      ${GNUWIN32_DIR}/lib
    )

    find_library(LIBCAMERAAPPS_OUTPUTS NAMES outputs
      PATHS
      ${_obLinkDir}
      ${GNUWIN32_DIR}/lib
    )

    find_library(LIBCAMERAAPPS_POST NAMES post_processing_stages
      PATHS
      ${_obLinkDir}
      ${GNUWIN32_DIR}/lib
    )

    find_library(LIBCAMERAAPPS_PREVIEW NAMES preview
      PATHS
      ${_obLinkDir}
      ${GNUWIN32_DIR}/lib
    )

    find_path(LIBCAMERAAPPS_INCLUDE_DIR core/libcamera_app.hpp
      PATH_SUFFIXES libcamera-apps
      ${_obIncDir}
      ${GNUWIN32_DIR}/include
    )

  if(LIBCAMERAAPPS_APPS AND LIBCAMERAAPPS_INCLUDE_DIR)
    set(LibCameraApps_FOUND TRUE)
  else (LIBCAMERAAPPS_APPS AND LIBCAMERAAPPS_INCLUDE_DIR)
    set(LibCameraApps_FOUND FALSE)
  endif(LIBCAMERAAPPS_APPS AND LIBCAMERAAPPS_INCLUDE_DIR)

  if (LibCameraApps_FOUND)
    if (NOT LibCameraApps_FIND_QUIETLY)
      message(STATUS "Found LIBCAMERAAPPS Library: ${LIBCAMERAAPPS_APPS}")
      message(STATUS "LIBCAMERAAPPS Include: ${LIBCAMERAAPPS_INCLUDE_DIR}")
    endif (NOT LIBCAMERAAPPS_FIND_QUIETLY)
  else (LibCameraApps_FOUND)
    if (LibCameraApps_FIND_REQUIRED)
      message(FATAL_ERROR "LIBCAMERAAPPS Library not found. Please install libcamera-apps")
    endif (LIBCAMERAAPPS_FIND_REQUIRED)
  endif (LibCameraApps_FOUND)

  mark_as_advanced(LIBCAMERAAPPS_INCLUDE_DIR LIBCAMERAAPPS_APPS LIBCAMERAAPPS_ENCODERS LIBCAMERAAPPS_IMAGES LIBCAMERAAPPS_OUTPUTS LIBCAMERAAPPS_POST LIBCAMERAAPPS_PREVIEW)
  
endif (LIBCAMERAAPPS_APPS AND LIBCAMERAAPPS_INCLUDE_DIR)
