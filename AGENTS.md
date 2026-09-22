# Project Instructions

## Read Before Changing Anything

- Read the technical document, hardware list, and open-source project index.
- Treat `Open-source projects/` as read-only reference material.
- Confirm the target file, existing interface, evidence source, and validation method before editing.

## Search And Reuse First

- Check ExoKit first, then local references, then current open-source projects and official documentation.
- Prefer direct reuse, then partial reuse, then design reuse.
- If no suitable implementation exists, explain that plainly and discuss it with the user before writing a module from scratch.
- Record the source and the adapted parts of reused code.

## Project Boundaries

- Fixed controller: STM32F446RE.
- J2 shoulder actuator: GIM6010-8.
- J3 elbow actuator: bare M3508 with C620; do not apply P19 gearbox claims.
- Do not invent wiring, protocol fields, dimensions, torque, thresholds, validation, or test results.
- Do not describe code, CAD fit-up, simulation, or bench work as body-worn or clinical validation.

## Editing Rules

- Keep one authoritative copy of project-owned files.
- Use ASCII, lower_snake_case names for new code files and directories.
- Keep application code outside generated STM32CubeMX initialization files.
- Only add CubeMX-preserved edits inside official `USER CODE BEGIN/END` regions.
- Make one focused change at a time and avoid unrelated refactors.
- Do not modify project code before identifying the root cause of a defect.
- After three unsuccessful fixes to the same issue, stop and discuss the design with the user.

## Verification And Reporting

- Build the affected firmware configuration and review new warnings when firmware changes exist.
- Verify drivers by parsing data before unloaded bench tests.
- Report changed files, behavioral changes, checks performed, and anything not verified.
- Update technical or interface documentation when hardware mappings, protocols, states, or directories change.

