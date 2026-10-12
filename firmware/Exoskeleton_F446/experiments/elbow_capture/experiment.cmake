option(EXPERIMENT_ELBOW_CAPTURE "Temporary CAN2 R2 fault recorder (no CAN commands)" OFF)
if(EXPERIMENT_ELBOW_CAPTURE)
    if(NOT S5_R2_ELBOW_TRIAL)
        message(FATAL_ERROR "EXPERIMENT_ELBOW_CAPTURE requires S5_R2_ELBOW_TRIAL=ON")
    endif()
    if(EXPERIMENT_CAN_BLACKBOX)
        message(FATAL_ERROR "CAN1 blackbox and elbow capture cannot share the console wrapper")
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../can_blackbox/recorder.c
        ${CMAKE_CURRENT_LIST_DIR}/capture.c)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../can_blackbox)
    foreach(symbol C620_Receive ElbowSessionAngle_Observe OutputGateway_Step clip_cmd_parse_line)
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--wrap=${symbol}")
    endforeach()
endif()
