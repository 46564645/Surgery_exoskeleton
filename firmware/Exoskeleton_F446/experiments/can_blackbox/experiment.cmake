option(EXPERIMENT_CAN_BLACKBOX "Temporary CAN1 fault recorder (no CAN commands)" OFF)
if(EXPERIMENT_CAN_BLACKBOX)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/recorder.c
        ${CMAKE_CURRENT_LIST_DIR}/stm32_capture.c)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    # GNU ld seams: no edits to main, generated IRQ handlers, HAL or protocol code.
    foreach(symbol HAL_CAN_IRQHandler HAL_CAN_GetRxMessage HAL_CAN_AddTxMessage
                   HAL_CAN_AbortTxRequest HAL_CAN_ResetError clip_cmd_parse_line)
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--wrap=${symbol}")
    endforeach()
endif()
