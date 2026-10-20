#ifndef SHOULDER_AMPLIFY_H
#define SHOULDER_AMPLIFY_H
/* L3: ExoKit AmplifyReaction::runLoop() decision logic for the shoulder.
 * Direction is tracked both ways (ExoKit BOTH), output only while raising.
 * The result is a rotor-side torque increment added on top of the gravity
 * baseline; it never replaces the baseline. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float increment_rotor_nm;      /* 0 = off. Unmeasured bench candidate. */
    float starting_velocity_rad_s; /* Joint speed that engages (ExoKit startingVelocity). */
    uint32_t suspension_ms;        /* ExoKit dirChangeSuspensionTime: re-engage delay and hold. */
    uint32_t ramp_ms;              /* Local addition: linear engage/release, ExoKit switches. */
} ShoulderAmplifyConfig;

typedef struct {
    bool engaged, raising, initial_movement, direction_changed;
    uint32_t direction_change_ms, engage_ms, step_ms;
    float scale;
} ShoulderAmplify;

bool ShoulderAmplify_ConfigValid(const ShoulderAmplifyConfig *config);
void ShoulderAmplify_Reset(ShoulderAmplify *amplify, uint32_t now_ms);
/* raise_velocity_rad_s is positive while the joint moves against gravity.
 * Returns the non-negative increment magnitude in rotor Nm. */
float ShoulderAmplify_Step(ShoulderAmplify *amplify, const ShoulderAmplifyConfig *config,
                           float raise_velocity_rad_s, uint32_t now_ms);
#endif
