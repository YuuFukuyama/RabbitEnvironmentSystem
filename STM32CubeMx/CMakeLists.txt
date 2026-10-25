cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32CubeMx)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# テストでダミーシナリオに戻すときは -DENV_SENSOR_USE_DUMMY=ON にする。既定は本番取得。
option(ENV_SENSOR_USE_DUMMY "温湿度をダミーシナリオで読む" OFF)
option(ENV_SENSOR_DUMMY_BOUNDARY_TEST "T-10/T-11 境界用ダミーシナリオ" OFF)
option(ENV_FAST_MEASURE "測定間隔 15 秒（境界試験用。本番記録に使わない）" OFF)
if(ENV_SENSOR_USE_DUMMY)
    set(ENV_SENSOR_SOURCE App/env_dummy.c)
else()
    set(ENV_SENSOR_SOURCE App/env_sht31.c)
endif()
if(ENV_SENSOR_DUMMY_BOUNDARY_TEST AND NOT ENV_SENSOR_USE_DUMMY)
    message(FATAL_ERROR "ENV_SENSOR_DUMMY_BOUNDARY_TEST は ENV_SENSOR_USE_DUMMY=ON が必要")
endif()

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    App/app_clock.c
    App/st7032.c
    App/board_led.c
    App/env_judge.c
    App/lcd_status.c
    App/app_cycle.c
    App/at_cmd.c
    App/relay_client.c
    ${ENV_SENSOR_SOURCE}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    App
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    $<$<BOOL:${ENV_SENSOR_USE_DUMMY}>:ENV_SENSOR_USE_DUMMY>
    $<$<BOOL:${ENV_SENSOR_DUMMY_BOUNDARY_TEST}>:ENV_SENSOR_DUMMY_BOUNDARY_TEST>
    $<$<BOOL:${ENV_FAST_MEASURE}>:MEASURE_INTERVAL_SEC=15>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
