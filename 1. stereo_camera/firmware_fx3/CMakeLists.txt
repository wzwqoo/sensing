#
# This is sample CMakeLists.txt uses fx3.cmake
#

cmake_minimum_required(VERSION 3.21)
include(${CMAKE_SOURCE_DIR}/fx3.cmake)
project(sensor_imx477)

#
# Options
# =======
# Code validation and CI server friendly
set(WPEDANTIC OFF                              CACHE BOOL    "turn on/off -Wpedantic option for compiller (default: off)")
set(WERROR    OFF                              CACHE BOOL    "turn on/off -Werror option for compiller (default: off)")

# Project settings
#fx3_enable_cxx()
#fx3_disable_stdc_libs()
fx3_enable_stdcxx_libs()

# Additional C++ flags
set(COMMON_FLAGS "-fmessage-length=0 -ffunction-sections -fdata-sections -fshort-wchar -Wall -Wextra")
if (WPEDANTIC)
  set(COMMON_FLAGS "${COMMON_FLAGS} -Wpedantic")
endif()
if (WERROR)
  set(COMMON_FLAGS "${COMMON_FLAGS} -Werror")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -fno-unwind-tables -std=c++11 ${COMMON_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS} -Wwrite-strings -std=c99")


# Add sources to executable
file(GLOB_RECURSE SOURCES
        "/firmware_fx3/source/*.c"
)
# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user defined include paths
        /firmware_fx3/source
)
