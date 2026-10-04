#include "s3_stop_input.h"

bool S3StopInput_ConfigReady(const BoardStopInputConfig *operator_input,
                           const BoardStopInputConfig *independent_input)
{
    return BoardStopInput_ConfigValid(operator_input)
        && BoardStopInput_ConfigValid(independent_input)
        && (operator_input->port != independent_input->port
            || operator_input->pin_mask != independent_input->pin_mask);
}

S3StopObservation S3StopInput_Read(const BoardStopInputConfig *operator_input,
                                 const BoardStopInputConfig *independent_input)
{
    S3StopObservation observation = {0};
    bool operator_active = false, independent_active = false;
    if (!S3StopInput_ConfigReady(operator_input, independent_input)
        || !BoardStopInput_Read(operator_input, &operator_active)
        || !BoardStopInput_Read(independent_input, &independent_active)) {
        return observation;
    }
    observation.available = true;
    observation.operator_stop_clear = !operator_active;
    observation.independently_stopped = independent_active;
    return observation;
}
