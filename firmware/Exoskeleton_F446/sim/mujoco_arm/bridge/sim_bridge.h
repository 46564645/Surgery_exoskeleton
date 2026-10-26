#ifndef SIM_BRIDGE_H
#define SIM_BRIDGE_H
/* Software-in-the-loop entry point for the MuJoCo simulation.
 *
 * The shared library links the real firmware control sources (App/control plus
 * s5_site.c) so the simulation runs the same code as the board. Every number
 * the simulation needs is read back through SimBridge_GetSite(); nothing is
 * duplicated in Python. All fields are 32-bit so the ctypes layout is
 * unambiguous; SimBridge_Sizes() reports the sizes for a runtime check. */
#include <stdint.h>

#if defined(_WIN32)
#define SIM_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define SIM_EXPORT __attribute__((visibility("default")))
#else
#define SIM_EXPORT
#endif

typedef struct {
    float shoulder_pos_rad;      /* Joint side, 0 = hanging session zero. */
    float shoulder_vel_rad_s;
    float elbow_pos_rad;
    float elbow_vel_rad_s;
    float force_n;               /* Interaction force, only read when force_valid. */
    uint32_t now_ms;
    int32_t shoulder_valid;      /* 0 = stale feedback. */
    int32_t elbow_valid;
    int32_t assist_enabled;      /* Bench A/B gate. */
    int32_t force_valid;         /* Force sample fresh and unsaturated. */
} SimBridgeInput;

typedef struct {
    float shoulder_rotor_nm;     /* Rotor side, before the 8:1 mapping. */
    float elbow_current_a;       /* C620 command. */
    int32_t ok;                  /* 0 = the controller rejected the model. */
} SimBridgeOutput;

typedef struct {
    float shoulder_rotor_per_joint, shoulder_rotor_zero_rad;
    int32_t shoulder_direction;
    float elbow_rotor_per_joint, elbow_rotor_zero_rad;
    int32_t elbow_direction;
    float elbow_motor_kt_nm_per_a;
    float shoulder_gain;
    float shoulder_gravity_amplitude_nm;
    float elbow_gravity_amplitude_nm;
    float shoulder_cap_rotor_nm, elbow_cap_a;
    float soft_speed_rad_s[2];
    float amplify_increment_rotor_nm;
    uint32_t send_interval_ms, session_ms, feedback_timeout_ms;
    uint32_t shoulder_property_period_ms, force_freshness_ms;
    int32_t dual, force_confirmed;
} SimBridgeSite;

/* Clears the Amplify and force-trigger state and latches the site profile. */
SIM_EXPORT void SimBridge_Reset(uint32_t now_ms);
/* Returns 1 when a command was produced, 0 when the controller rejected the
 * inputs (out is then zeroed). Never writes motor frames. */
SIM_EXPORT int32_t SimBridge_Step(const SimBridgeInput *in, SimBridgeOutput *out);
/* Copies the compiled site numbers so the simulation never re-declares them. */
SIM_EXPORT void SimBridge_GetSite(SimBridgeSite *out);
SIM_EXPORT void SimBridge_Sizes(int32_t *input_size, int32_t *output_size,
                                int32_t *site_size);

#endif
