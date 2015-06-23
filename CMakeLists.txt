cmake_minimum_required(VERSION 2.8)
# Create the project and build flags if they are not cached
project(idd)

set(INSTALL_PATH "${PROJECT_SOURCE_DIR}/installdir")

set(EXECUTABLE_OUTPUT_PATH "${INSTALL_PATH}/bin")
set(LIBRARY_OUTPUT_PATH "${INSTALL_PATH}/lib")

set(CMAKE_INSTALL_PREFIX ${INSTALL_PATH})
set(CAMERA_PATH "${PROJECT_SOURCE_DIR}/ext/mvIMPACT_acquire-x86_64-2.13.1/")
set(CAMERA_DRIVER_LIB_PATH "${PROJECT_SOURCE_DIR}/ext/mvIMPACT_acquire-x86_64-2.13.1/lib/x86_64")

# Set camera driver paths and libraries.
include_directories(${CAMERA_PATH}/apps/Common)
include_directories(${CAMERA_PATH}/mvDeviceManager/Include)
include_directories(${CAMERA_PATH}/mvPropHandling/Include)
include_directories(${CAMERA_PATH}/mvPropGrid/Include)
include_directories(${CAMERA_PATH})
link_directories(${CAMERA_DRIVER_LIB_PATH})

find_library(mvBlueFox NAMES "mvBlueFOX" PATHS ${CAMERA_DRIVER_LIB_PATH})
find_library(mvDeviceManager NAMES "mvDeviceManager" PATHS ${CAMERA_DRIVER_LIB_PATH})
find_library(mvPropHandling  NAMES "mvPropHandling" PATHS ${CAMERA_DRIVER_LIB_PATH})

add_subdirectory(libraries/camera_player)
include_directories(libraries)
add_subdirectory(apps/capture)
