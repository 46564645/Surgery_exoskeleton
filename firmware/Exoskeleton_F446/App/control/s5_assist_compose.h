#ifndef S5_ASSIST_COMPOSE_H
#define S5_ASSIST_COMPOSE_H
/* L3: pure composition of one S5/R5 assist command.
 *
 * This is the former body of s5_follow.c compute_target(), moved here so the
 * firmware and the MuJoCo software-in-the-loop build run the same code. Order
 * is unchanged: gravity baseline, Amplify increment, interaction-force gain,
 * assist gate, output caps.
 *
 * No state, no devices, no timing: the caller owns the Amplify and force
 * trigger state, decides whether the force sample is usable, and supplies the
 * site numbers. Units are rotor-side Nm for the shoulder and C620 A for the
 * elbow; the joint-side conversion stays with the callers. */
#include "force_assist.h"
#include "gravity_compensation.h"
#include "joint_state.h"
#include "s5_elbow_support.h"
#include "shoulder_amplify.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool dual;                                 /* Both joints, else elbow only. */
    const GravityCompensationConfig *shoulder_gravity;
    const GravityCompensationConfig *elbow_gravity;  /* dual only */
    const S5ElbowSupportConfig *elbow_support;       /* elbow-only mode */
    const ShoulderAmplifyConfig *shoulder_amplify;   /* dual only, 0 = off */
    const ForceAssistConfig *force_assist;           /* dual only, off = unconfirmed */
    float shoulder_gain;
    float elbow_motor_kt_nm_per_a;
    float shoulder_rotor_per_joint;
    float shoulder_rotor_cap_nm;                     /* dual only */
    float elbow_current_cap_a;                       /* dual only */
} S5AssistComposeConfig;

typedef struct {
    float shoulder_rotor_nm;
    float elbow_current_a;
} S5AssistComposeOutput;

/* Returns false when a required input is missing or the model is invalid; the
 * output is then cleared, matching the previous FOLLOW behaviour. A rejected
 * force sample only drops the increment, never the baseline. */
bool S5AssistCompose_Update(const S5AssistComposeConfig *config,
                            const JointState *shoulder, const JointState *elbow,
                            bool assist_enabled, bool force_valid, float force_n,
                            ShoulderAmplify *amplify, ForceAssistState *force,
                            uint32_t now_ms, S5AssistComposeOutput *out);

#endif
