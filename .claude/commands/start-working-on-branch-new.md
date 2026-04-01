# Start Working on Branch

## Input
Branch name: $ARGUMENTS

## Setup Phase

1. **Checkout branch**:
   ```bash
   git fetch origin
   git checkout $ARGUMENTS 2>/dev/null || git checkout -b $ARGUMENTS origin/$ARGUMENTS 2>/dev/null || echo "Using existing local branch"
   ```

2. **Load session context**: Find `.claude/sessions/context_session_*.md` matching this branch. Load the full implementation plan, selected agents, and architectural decisions.

3. **Detect project type**:
   ```bash
   [ -f "nest-cli.json" ] && echo "NestJS"
   [ -f "artisan" ]       && echo "Laravel"
   [ -f "angular.json" ]  && echo "Angular"
   [ -f "pubspec.yaml" ]  && echo "Flutter"
   find . -maxdepth 2 -name "*.ino" | grep -q . && echo "Arduino/Embedded"
   ```

4. **Verify planning**: Confirm the session has a complete plan with selected agents and, for embedded projects, the timing budget, failsafe matrix, and FreeRTOS decision.

---

## Pre-Implementation: Use Tools

### Sequential Thinking (`mcp__sequentialthinking__sequentialthinking`)
Before writing any code for non-trivial changes, use sequential thinking to:
- Confirm the implementation order (what must be done first?)
- Identify hidden dependencies between modules
- Reason through any design decision that has multiple valid approaches
- Validate that the plan's timing budget and safety requirements will be met

### Context7 (`mcp__context7__resolve-library-id` + `mcp__context7__get-library-docs`)
Before using any library API, verify it with Context7:
- Confirm correct function signatures and parameters
- Check for known breaking changes or version-specific behaviour
- Verify interrupt-safe API usage (e.g., `xQueueSendFromISR`, `IRAM_ATTR`)
- Look up platform-specific details (ESP32 LEDC vs AVR analogWrite, etc.)

---

## Implementation Phase

### Web Projects

1. **Use session agents**:
   - **NestJS**: `nestjs-backend-architect` for Clean Architecture patterns
   - **Laravel**: `laravel-backend-architect` for MVC and API patterns
   - **Angular**: `angular-frontend-developer` for reactive patterns
   - **Flutter**: `flutter-frontend-developer` for mobile patterns

2. **TDD**: write tests first, run constantly (`npm test` / `php artisan test` / `flutter test`), ensure >80% coverage.

3. **Follow session plan**: Clean Architecture layers, SOLID principles, framework-specific patterns.

### Embedded Projects

1. **Use `arduino-embedded-developer`** for: new subsystem, state machine change, control loop, FreeRTOS task, interrupt design, failsafe update, memory analysis. Implement directly for trivial changes (adjusting a constant, adding a Serial.print).

2. **Enforce embedded conventions**:
   - Zero `delay()` — all timing via `millis()`, hardware timers, or `vTaskDelayUntil()`
   - No `String` class or dynamic allocation on AVR platforms
   - Every ISR: minimal body (flag/counter only), marked `IRAM_ATTR` on ESP32
   - Every `begin()` call: check return value, handle failure
   - FreeRTOS shared data: always protected by mutex or queue
   - Hardware watchdog enabled for any motor/actuator project
   - Failsafe safe-state triggered within one `loop()` cycle of any fault

3. **Compile check after each logical change**:
   ```bash
   SKETCH=$(find . -name "*.ino" -maxdepth 2 | head -1)
   arduino-cli compile --fqbn <FQBN> "$(dirname $SKETCH)" 2>&1
   # If arduino-cli unavailable: Arduino IDE Verify (Ctrl+R)
   ```

4. **Static checks**:
   ```bash
   grep -rn "delay(" . --include="*.ino" --include="*.cpp" --include="*.h"
   grep -rn "\bString\b" . --include="*.ino" --include="*.cpp"  # AVR only
   grep -rn "new \|malloc(" . --include="*.ino" --include="*.cpp"  # AVR only
   ```

---

## Commit and Push

```bash
git add -p   # stage selectively
git commit -m "feat: <short description>"
git push origin $ARGUMENTS
```

Create or update PR targeting `main`:
```bash
gh pr create --title "<title>" --base main --head $ARGUMENTS --body "<body>" 2>/dev/null \
  || gh pr edit --body "<updated body>"
```

---

## Status Report

```
IMPLEMENTATION SUMMARY
======================

Branch:  $ARGUMENTS
Stack:   [NestJS / Laravel / Angular / Flutter / Arduino + <board>]

Requirements implemented:
  - req 1
  - req 2

Requirements pending:
  - req 1 (reason)

# Web
Tests: [N passed / N failed]
Coverage: [X%]
Build: ✅ PASS / ❌ FAIL

# Embedded
Compile check:       ✅ PASS / ❌ FAIL
delay() check:       ✅ None / ❌ Lines: [N]
String/malloc check: ✅ None / ⚠️  Found
Watchdog present:    ✅ Yes / ⚠️  No

Overall status: [Needs More Work / All Completed]
PR: <github-pr-url>
```

---

## PR Management
1. Monitor: `gh pr view $PR_NUMBER --json statusCheckRollup,state,mergeable,url`
2. Address CI failures or conflicts immediately
3. Use `update-feedback` command for reviewer changes
4. PR targets `main`, requires 1 approving review

## Completion Criteria
- ✅ All requirements implemented
- ✅ Web: >80% test coverage, CI green | Embedded: compiles, no `delay()`, watchdog active
- ✅ Code follows project architectural patterns and CLAUDE.md conventions
- ✅ CLAUDE.md updated if new pins, commands, or constants added (embedded)
- ✅ PR reviewed and approved, no merge conflicts

## Notes
- Use sequential thinking before any complex architectural decision
- Use Context7 before calling any library API you haven't verified
- Always use `gh` CLI for GitHub operations
- Never introduce `delay()` in embedded projects
