# Create New GitHub Branch

## Input
Feature/Bug/Chore description: $ARGUMENTS

## Step 1: Retrieve Planning Information
1. **Load Session File**: Check for existing `.claude/sessions/context_session_{feature_name}.md` from explore-plan
2. **Extract Requirements**: Get detailed implementation plan and selected agents
3. **Detect Technology Stack**:
   - Backend: NestJS or Laravel
   - Frontend: Angular or Flutter
   - Embedded: Arduino/microcontroller (`.ino` files present)
4. **Generate Branch Name**: Create conventional branch name from feature description

## Step 2: Create GitHub Issue

Using information from the explore-plan session, create a comprehensive GitHub issue:

```bash
gh issue create --title "[TYPE] $ARGUMENTS" --body "$(cat <<'EOF'
### 📋 Problem Statement
- What problem does this solve?
- What are the current limitations or issues?
- Why is this important now?

### 🎯 User / System Value
- What specific benefits will users or the system get?
- Provide concrete examples and scenarios

### 🔧 Technical Requirements
**Web projects:**
- Architecture considerations
- Database changes needed (if any)
- API endpoints to create/modify
- UI components and pages required

**Embedded projects:**
- Target platform (ESP32 / Arduino Uno / Arduino Mega / RP2040 / other)
- Affected modules / subsystems
- Pin assignments or PWM changes (if any)
- Communication protocol changes (new commands, format)
- Timing budget impact — must remain non-blocking

### ✅ Definition of Done
**Web:**
- [ ] Implementation complete following project architecture principles
- [ ] Unit tests added with >80% coverage
- [ ] Integration tests for main user flows
- [ ] Code review approved by 1 reviewer
- [ ] All CI/CD checks pass (build, test, lint)

**Embedded:**
- [ ] Compiles without errors (`arduino-cli compile` or Arduino IDE Verify)
- [ ] No `delay()` calls introduced — all timing via `millis()`
- [ ] No `String` class or dynamic allocation (if AVR target)
- [ ] CLAUDE.md updated if pins, commands, or key constants changed
- [ ] Code review approved by 1 reviewer

### 🧪 Testing / Validation Checklist
**Web — manual testing:**
- [ ] [Step-by-step testing instructions]
- [ ] [Expected outcomes]

**Embedded — hardware testing:**
- [ ] [Step-by-step test on the physical device]
- [ ] [Expected behaviour for each step]
- [ ] [Edge cases: power cycle, disconnection, sensor out-of-range]

### 🏗️ Implementation Strategy
- Branch name: `feat/feature-name-kebab-case`
- Target branch: `main`
- Dependencies: [List any blocking issues or requirements]
EOF
)"
```

## Step 3: Create Feature Branch

```bash
FEATURE_NAME=$(echo "$ARGUMENTS" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/-/g' | sed 's/--*/-/g' | sed 's/^-\|-$//g')
BRANCH_NAME="feat/$FEATURE_NAME"

git fetch origin
git checkout main 2>/dev/null || git checkout -b main
git pull origin main 2>/dev/null || echo "No remote main branch, continuing with local"

git checkout -b $BRANCH_NAME main
echo "✅ Created branch: $BRANCH_NAME"
```

## Step 4: Save Branch Info
Update session file with the issue number and branch name.

**Next step**: `start-working-on-branch-new $BRANCH_NAME` to begin implementation

## Quality Checklist
- ✅ Problem clearly defined
- ✅ Technical requirements specific to project stack
- ✅ Definition of Done uses the correct checklist (web vs embedded)
- ✅ Testing steps are concrete and actionable
- ✅ Branch follows `feat/feature-name` naming
