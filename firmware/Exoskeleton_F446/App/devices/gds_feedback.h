#ifndef GDS_FEEDBACK_H
#define GDS_FEEDBACK_H
/* L2 experimental rev2.2 SIMPLE decoder; NOT a production permit source.
 * L1 has already rejected extended/RTR frames. No CAN writes or mode changes. */
#include "board_can.h"
typedef enum { GDS_PROFILE_UNSELECTED, GDS_PROFILE_V22_FW_GE_0513 } GdsProfile;
typedef struct {
    float rotor_position_rad, rotor_velocity_rad_s;
    uint32_t estimate_ms, heartbeat_ms, axis_error;
    uint8_t axis_state, fault_flags, life;
    bool has_estimate, has_heartbeat, life_contiguous;
} GdsFeedback;
typedef struct {
    GdsFeedback sample;
    GdsProfile profile;
    uint8_t node;
    uint32_t timeout_ms;
    bool configured;
} GdsDevice;
bool GdsFeedback_Init(GdsDevice *device, uint8_t node, GdsProfile profile, uint32_t timeout_ms);
bool GdsFeedback_Receive(GdsDevice *device, BoardCanBus bus, const BoardCanFrame *frame, uint32_t now_ms);
/* Only wire validity/freshness. Does not confirm physical angle, turn origin,
 * firmware compatibility, mode, calibration or driver readiness. */
bool GdsFeedback_Get(const GdsDevice *device, uint32_t now_ms, GdsFeedback *out);
#endif
