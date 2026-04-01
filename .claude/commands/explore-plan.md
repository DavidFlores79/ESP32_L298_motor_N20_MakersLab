<user_request>
#$ARGUMENTS
<user_request>

At the end of this message, I will ask you to do something. Please follow the "Explore, Team Selection, Plan, Branch Strategy, Advice, Update, Clarification and Iterate" workflow when you start over the user_request.

# Step 0: Sequential Thinking (always first)
Before doing anything else, use `mcp__sequentialthinking__sequentialthinking` to break down the request into its core concerns:
- What is the complexity level? (simple tweak / new feature / new subsystem / full architecture)
- What are the main risks or unknowns?
- What questions must be answered before a solid plan can be written?

This step ensures the plan addresses root causes, not just surface symptoms.

# Create the session file
Create `.claude/sessions/context_session_{feature_name}.md` where plan is going to be updated with all the future iterations and feedback.

# Explore
Explore the relevant files in the repository to understand:
- Current project structure and technology stack
- Existing architectural patterns and conventions
- Dependencies, libraries, and their versions
- Related existing features

For **embedded projects** (`.ino` files detected), additionally explore:
- Platform and board (ESP32, Arduino Uno/Mega, RP2040, etc.)
- All existing state machines, timing patterns, and pin/PWM assignments
- Communication interfaces in use (Serial, I2C, SPI, BLE, WiFi, etc.)
- Any FreeRTOS tasks already in use
- Current safety/watchdog strategy

Use **Context7** to verify documentation for any library or framework involved:
```
mcp__context7__resolve-library-id → mcp__context7__get-library-docs
```
Do this for every library the plan will depend on before finalizing recommendations.

# Team Selection (parallel execution if possible)
Select subagents based on the technology stack:
- **Backend**: `nestjs-backend-architect` (NestJS/TypeScript) or `laravel-backend-architect` (Laravel/PHP)
- **Frontend**: `angular-frontend-developer` (Angular/TypeScript) or `flutter-frontend-developer` (Flutter/Dart)
- **Embedded**: `arduino-embedded-developer` for any Arduino / ESP32 / microcontroller project (`.ino` files, or microcontroller-specific libraries/toolchain)
  - Use for: new subsystems, control loops, FreeRTOS design, interrupt strategy, safety/failsafe, multi-file architecture, any complexity above a simple constant change

Don't invoke them yet — only identify who you'll consult and for what specific aspects.

# Plan
Write a detailed, production-ready implementation plan:

## For web projects
- Feature requirements and acceptance criteria
- Database schema changes (if needed)
- API endpoint design
- UI/UX components and user flows
- Testing strategy (unit, integration, e2e)
- Performance considerations

## For embedded projects — required sections
- **Files to create/modify**: every `.h`/`.cpp`/`.ino` change
- **Timing budget**: all periodic tasks with interval, max execution time, and trigger type
- **Control loop design** (if applicable): frequency, sensor rate, PID parameters, anti-windup, output saturation, jitter tolerance
- **FreeRTOS decision**: justify cooperative `loop()` vs FreeRTOS tasks; if tasks are used, define each task's core, priority, stack size, and inter-task communication
- **Interrupt strategy**: which signals need ISRs, ISR content (flag/counter only), volatile data + critical section design
- **Pin/timer assignments**: full table including conflict analysis (LEDC channels, shared timers, ADC1 vs ADC2 on ESP32)
- **Failsafe matrix**: every failure mode → detection → response → degraded mode decision
- **Memory analysis**: SRAM/Flash estimate, buffer sizes, heap fragmentation risks (ESP32)
- **Production observability**: heap monitoring, reset reason logging, uptime/error counters, LED strategy
- **Code conventions in this project**: check existing comments, identifier style, and follow them

If there are unresolved questions, pause and ask before continuing.

# Branch Strategy
- **Branch Name**: `feat/{feature-name-kebab-case}`
- **Base Branch**: `main`
- **Target Branch**: `main`
- **Review Requirements**: 1 reviewer required before merging

# Advice
Use selected subagents in parallel to get architectural guidance:
- Backend/Frontend architects for web stack decisions
- `arduino-embedded-developer` for embedded architecture, non-blocking design, safety systems, and library verification via Context7

If uncertain about any technical decision, use parallel subagents for web research.

# Update
Update the session file with the final plan:
- Complete implementation roadmap
- Branch name
- All architectural decisions with rationale
- Technology-specific file structure, timing tables, pin tables, failsafe matrix

# Clarification
Ask questions in A) B) C) format about anything unclear:
- User scenarios and edge cases
- For embedded: target platform, available pins, power constraints, required control loop frequency, safety/failsafe requirements
- Integration requirements with existing systems
- Technology preferences (if ambiguous)

IMPORTANT: Wait for answers before continuing.

# Iterate
Evaluate and refine until the plan is complete, unambiguous, and production-ready.

# RULES
- Goal: create the comprehensive plan — DO NOT implement
- Branch naming: always `feat/{feature-name}`
- Target branch: always `main`
- **Embedded**: zero `delay()`; all timing via `millis()`, hardware timers, or `vTaskDelayUntil()`
- **Embedded**: no `String` class or dynamic allocation on AVR
- **Embedded**: every ISR must be minimal — flag/counter only
- **Embedded**: every failure mode must have a defined safe-state response
- **All projects**: use sequential thinking for complex decisions; use Context7 to verify library APIs
