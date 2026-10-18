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
set(CMAKE_PROJECT_NAME Exoskeleton_F446)

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

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/board_console.c
    Core/Src/board_runtime.c
    Core/Src/board_can.c
    Core/Src/board_can_tx.c
    Core/Src/board_stop_input.c
    Core/Src/board_hx711.c
    App/devices/hx711.c
    App/application/app_main.c
    App/application/console_commands.c
    App/application/s3_console.c
    App/application/can_monitor.c
    App/application/c620_standstill.c
    App/application/elbow_session_angle.c
    App/devices/c620_feedback.c
    App/devices/motor_command.c
    App/devices/gds_feedback.c
    App/devices/gds_sdo_read.c
    App/application/shoulder_read_diagnostic.c
    App/application/shoulder_qualification.c
    App/application/encoder_stream.c
    App/application/shoulder_property_feedback.c
    App/control/joint_state.c
    App/control/gravity_compensation.c
    App/control/s5_elbow_support.c
    App/control/safety_supervisor.c
    App/application/output_gateway.c
    App/application/output_can_transport.c
    App/application/s3_bench.c
    App/application/s3_site.c
    App/application/s4_site.c
    App/application/s4_follow.c
    App/application/s5_site.c
    App/application/s5_follow.c
    App/application/s3_feedback.c
    App/application/shoulder_standstill.c
    App/application/s3_shoulder_service.c
    App/application/s3_stop_input.c
    third_party/lwrb/src/lwrb/lwrb.c
    third_party/clip/src/clip_cmd_parse.c
    third_party/clip/src/clip_cmd_call.c
    third_party/clip/src/clip_notify.c
    third_party/clip/src/clip_utils_arg.c
    third_party/clip/src/clip_utils_parse.c
    third_party/clip/src/clip_utils_hex.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App/application
    App/devices
    App/control
    third_party/lwrb/src/include
    third_party/clip/src
)

# Add project symbols (macros)
option(S5_R2_ELBOW_TRIAL "Build the isolated one-shot elbow direction/stop candidate" OFF)
option(S5_R4_ELBOW_FOLLOW "Build the isolated elbow-only FOLLOW candidate" OFF)
option(S5_R5_DUAL_FOLLOW "Build the isolated low-pose dual-axis FOLLOW candidate" OFF)
set(S5_R5_ELBOW_SUPPORT_SIGN "" CACHE STRING "R5 dual FOLLOW elbow-current sign, explicitly 1 or -1 after direction observation")
set(S5_R5_ELBOW_SUPPORT_MA "" CACHE STRING "R5 dual FOLLOW elbow-current target in mA, explicitly 1..20000; command cap is 20 A")
if(S5_R5_DUAL_FOLLOW)
    if(S5_R2_ELBOW_TRIAL OR S5_R4_ELBOW_FOLLOW)
        message(FATAL_ERROR "R5 dual FOLLOW must use a separate build from R2 and R4")
    endif()
    if(NOT "${S5_R5_ELBOW_SUPPORT_SIGN}" MATCHES "^(1|-1)$")
        message(FATAL_ERROR "R5 dual FOLLOW requires S5_R5_ELBOW_SUPPORT_SIGN=1 or -1 after direction observation")
    endif()
    if(NOT "${S5_R5_ELBOW_SUPPORT_MA}" MATCHES "^[1-9][0-9]?[0-9]?[0-9]?[0-9]?$"
        OR S5_R5_ELBOW_SUPPORT_MA GREATER 20000)
        message(FATAL_ERROR "R5 dual FOLLOW requires S5_R5_ELBOW_SUPPORT_MA=1..20000")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        S5_R5_DUAL_FOLLOW S5_R5_ELBOW_SUPPORT_SIGN=${S5_R5_ELBOW_SUPPORT_SIGN}
        S5_R5_ELBOW_SUPPORT_MA=${S5_R5_ELBOW_SUPPORT_MA})
endif()
set(S5_R4_ELBOW_SUPPORT_SIGN "" CACHE STRING "R4 elbow support-current sign, explicitly 1 or -1 after R2 direction observation")
set(S5_R4_ELBOW_SUPPORT_MA "" CACHE STRING "R4 elbow support-current magnitude in mA, explicitly 1..4000 after R2 observation")
if(S5_R4_ELBOW_FOLLOW)
    if(S5_R2_ELBOW_TRIAL)
        message(FATAL_ERROR "R2 elbow trial and R4 elbow FOLLOW must use separate builds")
    endif()
    if(NOT "${S5_R4_ELBOW_SUPPORT_SIGN}" MATCHES "^(1|-1)$")
        message(FATAL_ERROR "R4 elbow FOLLOW requires S5_R4_ELBOW_SUPPORT_SIGN=1 or -1 after R2 direction observation")
    endif()
    if(NOT "${S5_R4_ELBOW_SUPPORT_MA}" MATCHES "^[1-9][0-9]?[0-9]?[0-9]?$"
        OR S5_R4_ELBOW_SUPPORT_MA GREATER 4000)
        message(FATAL_ERROR "R4 elbow FOLLOW requires S5_R4_ELBOW_SUPPORT_MA=1..4000 after R2 observation")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        S5_R4_ELBOW_FOLLOW S5_R4_ELBOW_SUPPORT_SIGN=${S5_R4_ELBOW_SUPPORT_SIGN}
        S5_R4_ELBOW_SUPPORT_MA=${S5_R4_ELBOW_SUPPORT_MA})
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    $<$<BOOL:${S5_R2_ELBOW_TRIAL}>:S5_R2_ELBOW_TRIAL>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Temporary diagnostics: deleting this experiment directory is supported.
include(experiments/can_blackbox/experiment.cmake OPTIONAL)
include(experiments/elbow_capture/experiment.cmake OPTIONAL)
