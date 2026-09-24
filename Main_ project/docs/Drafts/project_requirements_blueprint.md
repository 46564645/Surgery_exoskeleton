# Project Requirements Blueprint

> Draft status: first requirements draft from `grill-with-docs`.
> This document states what the project should demonstrate and what is out of scope.
> It intentionally does not choose the final technical route. Open-source reuse and implementation choices should be decided later through `wayfinder`.

## 1. Project Intent

This project is a student prototype of an upper-limb assistive exoskeleton for orthopedic-operation-style upper-limb workload scenarios.

The system should demonstrate that an arm or arm-like linkage can receive posture-dependent assistance, remain repositionable by the user, and later receive extra assistance when the user applies additional force.

The project is not a commercial medical device and does not claim clinical readiness.

## 2. Target Users And First-Version Focus

The full project concept serves two user groups:

- Surgical-assistant-type users who need help holding posture for long periods, such as holding retractors, scopes, or tools.
- Surgeon/operator-type users who may need short bursts of extra help during forceful actions such as pushing, prying, drilling, or similar operations.

The first version should not become two separate systems. It should focus on posture holding and reduced effort as the primary demonstration. Active force assistance remains part of the same system concept, but is a later extension on top of the posture-support behavior.

## 3. First-Version Demonstration Platform

The first accepted demonstration should use a bench fake arm or linkage, not a human-worn setup.

The bench setup should represent the relevant arm or limb segment well enough to demonstrate assistance behavior, repositioning behavior, and hardware protection behavior. Wearable arm demonstration is the later whole-project endpoint, after bench behavior is clear.

## 4. Functional Priorities

### P0: Required For The First Accepted Bench Demonstration

#### P0.1 Baseline Gravity Compensation

The system must provide baseline gravity compensation as an always-present assistance behavior.

Gravity compensation means posture-dependent assistance intended to reduce the effort needed to hold the fake arm/linkage against gravity. It should not be only a mode name or a fixed output. The output should change with posture or with a posture-derived compensation rule.

The first version does not need to prove an exact compensation percentage such as 80 percent. It must show that assistance is posture-related, explainable, and visibly or measurably reduces required holding effort.

#### P0.2 Follow Without Obvious Blocking

The user must be able to push or reposition the fake arm/linkage without the system obviously fighting normal movement.

The first version does not need commercial-grade smoothness. It may lag or feel prototype-like, but it should not behave as a rigid lock that prevents repositioning. After the user stops repositioning, the system should return to providing support.

#### P0.3 Hardware Protection

The first version must protect the project hardware during bench testing.

Minimum hardware-protection requirements:

- Output command limiting to avoid excessive current, torque, or actuator command.
- Angle, travel, or mechanical range limiting to avoid hitting the mechanism.
- Communication or sensing timeout behavior that stops or reduces output.
- A quick-stop or emergency-stop method for bench testing.
- Temperature or voltage checks if the selected hardware and reference implementation expose them conveniently.

This is hardware protection for a student prototype. It is not a claim of human safety or medical-device safety.

### P1: Important But Not The First Gate

P1 work improves the quality of the prototype but should not block the first accepted P0 bench demonstration unless it is needed to make P0 observable.

Possible P1 items:

- Better smoothing during posture transitions.
- Better logging or visualization of posture, command, and assistance output.
- Stronger calibration workflow for the fake arm/linkage.
- More repeatable measurement setup for assisted versus unassisted holding effort.

### P2: Later First-Version Extension

#### P2.1 Active Assist

Active assist is extra assistance added on top of always-on baseline gravity compensation when the user applies additional force.

The current architecture detects the intended assist direction and entry/exit trend from each actuator's joint-position and joint-velocity feedback. In the first version, active assist may be fixed extra assistance rather than continuously proportional to a measured external force.

The fixed 10 SPS HX711 force-sensor path is an auxiliary monitoring and evidence channel. It does not trigger active assist and is not a safety input. Manual commands remain available for isolated calibration, debugging, and bench verification.

## 5. Evidence And Acceptance

The first accepted bench demonstration should provide evidence for the P0 requirements.

### Gravity Compensation Evidence

Required evidence:

- Compare assisted versus unassisted effort at the same posture.
- Use simple force measurement where practical.
- Record video showing the fake arm/linkage behavior at multiple postures.

The project should prefer simple, visible, repeatable evidence over complex claims.

### Posture Coverage

The first version should cover a continuous shoulder-related posture range, especially shoulder raising/lowering related behavior.

It does not need to cover the full human arm workspace, dual-arm movement, or every shoulder/elbow combination in the first version.

### Follow Evidence

The first version should show that a user can push or reposition the bench fake arm/linkage without obvious opposing resistance, and that support returns after repositioning.

### Active Assist Evidence

Active assist evidence is not the first P0 gate. When implemented, it should show extra assistance added on top of baseline gravity compensation and demonstrate entry, stop, reversal, and return-to-baseline behavior using joint feedback.

## 6. Final Project Endpoint

The overall project endpoint is a wearable arm demonstration that shows:

- Baseline gravity compensation.
- Active assist added on top of baseline gravity compensation.
- A reasonable explanation of how joint posture and motion feedback relate to assistance output, with force sensing retained as auxiliary observation.
- Bench validation evidence before wearable demonstration.

This endpoint is still a student prototype demonstration. It is not real surgery validation, medical-device validation, or clinical proof.

## 7. Out Of Scope For The First Version

The first version does not include:

- Real surgery environment validation.
- Medical-device-grade safety, certification, or regulatory work.
- Full human-wear safety validation.
- Dual-arm or full-body exoskeleton behavior.
- Precise human biomechanical modeling.
- A final technical route or final algorithm choice before wayfinding.

## 8. Technical Route Policy

This requirements blueprint does not decide the implementation route.

For each major function, the next stage should use `wayfinder` to investigate and decide the technical route. That investigation should prioritize direct open-source reuse where possible.

The open-source reuse rule is:

1. Check the existing local reference projects and documents.
2. If they do not provide a directly reusable function, search online for the closest directly reusable open-source project or implementation.
3. Prefer direct reuse, then partial reuse, then design/logic reuse.
4. If no suitable reusable project is found, report that clearly to the user before designing the function from scratch.

This rule applies especially to gravity compensation, actuator control, joint-feedback-triggered active assist, and embedded module architecture.

## 9. Relationship To Existing Technical Documentation

The existing technical document contains useful project background and functional intent, but its concrete technical route, algorithm, and architecture descriptions are not authoritative. The current technical route and architecture are defined by [`system_architecture_blueprint.md`](system_architecture_blueprint.md).

This requirements blueprint should be treated as the current source of truth for:

- What the project needs to demonstrate.
- Which functions are P0/P1/P2.
- What first-version evidence is required.
- What is out of scope.
- What should be deferred to technical-route wayfinding.

The technical document should be rewritten or revised against the system architecture blueprint before its architecture sections are treated as current.
