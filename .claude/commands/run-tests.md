# Run Comprehensive Test Suite

## Input
Test scope (optional): $ARGUMENTS
- `all` - Run all test types (default)
- `unit` - Run only unit tests
- `integration` - Run only integration tests
- `e2e` - Run only end-to-end tests
- `coverage` - Run tests with detailed coverage report

## Step 1: Detect Project Technology Stack
Automatically detect the technology being used:

### Backend Detection
1. **NestJS Project**: Look for `nest-cli.json`, `@nestjs/core` in package.json
2. **Laravel Project**: Look for `composer.json` with Laravel framework, `artisan` file

### Frontend Detection
1. **Angular Project**: Look for `angular.json`, `@angular/core` in package.json
2. **Flutter Project**: Look for `pubspec.yaml`, `flutter` SDK dependency

### Embedded Detection
1. **Arduino/Microcontroller Project**: Look for `*.ino` files in the project root or subdirectories

### Mixed Projects
- Detect multiple technologies in monorepo structure
- Run tests for each detected technology stack

## Step 2: Technology-Specific Test Execution

### 🎯 **NestJS Backend Tests**
```bash
npm run test
npm run test:e2e
npm run test:cov
npm run test:watch
```
**Coverage Requirement:** >80% | **Frameworks:** Jest, Supertest

### 🎯 **Laravel Backend Tests**
```bash
php artisan test --testsuite=Feature
php artisan test --testsuite=Unit
php artisan test --coverage --min=80
php artisan test --parallel
```
**Coverage Requirement:** >80% | **Frameworks:** PHPUnit, Pest

### 🎯 **Angular Frontend Tests**
```bash
ng test --watch=false --browsers=ChromeHeadless
ng test --code-coverage --watch=false --browsers=ChromeHeadless
ng e2e
ng lint && ng test --watch=false --browsers=ChromeHeadless
```
**Coverage Requirement:** >80% | **Frameworks:** Jasmine, Karma, Cypress

### 🎯 **Flutter Frontend Tests**
```bash
flutter test
flutter test --coverage
genhtml coverage/lcov.info -o coverage/html
flutter test integration_test/
```
**Coverage Requirement:** >80% | **Frameworks:** flutter_test, mockito, bloc_test

### 🎯 **Arduino / Microcontroller Firmware**

Detection: one or more `*.ino` files found in the project.

```bash
find . -maxdepth 2 -name "*.ino" | head -5
```

#### Compilation check
```bash
arduino-cli version 2>/dev/null || echo "arduino-cli not found — use Arduino IDE Verify instead"

# Detect board
arduino-cli board list
arduino-cli board listall 2>/dev/null | grep -i "esp32\|avr\|samd\|rp2040" | head -10

# Compile — replace <FQBN> (common: esp32:esp32:esp32 | arduino:avr:uno | arduino:avr:mega | rp2040:rp2040:rpipico)
SKETCH=$(find . -name "*.ino" -maxdepth 2 | head -1)
arduino-cli compile --fqbn <FQBN> "$(dirname $SKETCH)" 2>&1
```

#### Static code review
```bash
# No blocking delay() — replace with millis()-based timing
grep -rn "delay(" . --include="*.ino" --include="*.cpp" --include="*.h"

# No String class on AVR (heap fragmentation — use char[] instead)
grep -rn "\bString\b" . --include="*.ino" --include="*.cpp" --include="*.h"

# No dynamic allocation on AVR
grep -rn "new \|malloc\|calloc\|realloc" . --include="*.ino" --include="*.cpp"

# ISRs should only set a flag — review any long ISR body
grep -rn "ISR\|IRAM_ATTR" . --include="*.ino" --include="*.cpp" -A 8
```

#### Production observability checks
```bash
# ESP32: heap monitoring present?
grep -rn "getFreeHeap\|getMinFreeHeap" . --include="*.ino" --include="*.cpp"

# ESP32: reset reason logged on startup?
grep -rn "esp_reset_reason\|resetReason" . --include="*.ino" --include="*.cpp"

# Watchdog enabled?
grep -rn "wdt\|esp_task_wdt\|enableLoopWDT" . --include="*.ino" --include="*.cpp" -i

# Failsafe / communication timeout present?
grep -rn "cmdTimeout\|watchdog\|safeStop\|emergencyStop\|lastMoveTime\|lastCmdTime" . --include="*.ino" --include="*.cpp" -i

# Debug output guarded by flag (not always-on in production)?
grep -rn "DEBUG_ENABLED\|LOG_LEVEL\|ifdef DEBUG" . --include="*.ino" --include="*.cpp" --include="*.h"
```

**Report format:**
```
🎯 Arduino/Embedded Firmware:
├── Compilation:           ✅ PASS / ❌ FAIL
├── delay() calls:         ✅ None / ❌ Lines: [N]
├── String (AVR):          ✅ None / ⚠️  Found
├── Dynamic alloc (AVR):   ✅ None / ⚠️  Found
├── ISR review:            ✅ Minimal / ⚠️  Review needed
├── Heap monitoring:       ✅ Present / ⚠️  Missing (ESP32)
├── Reset reason log:      ✅ Present / ⚠️  Missing (ESP32)
├── Watchdog:              ✅ Enabled / ⚠️  Not found
├── Failsafe / timeout:    ✅ Present / ⚠️  Missing
└── Debug guards:          ✅ Guarded / ⚠️  Always-on Serial
```

## Step 3: Execute Tests by Technology

```bash
# Check for NestJS
if [ -f "nest-cli.json" ]; then echo "🎯 NestJS"; fi

# Check for Laravel
if [ -f "artisan" ]; then echo "🎯 Laravel"; fi

# Check for Angular
if [ -f "angular.json" ]; then echo "🎯 Angular"; fi

# Check for Flutter
if [ -f "pubspec.yaml" ]; then echo "🎯 Flutter"; fi

# Check for Arduino / Embedded
if find . -maxdepth 2 -name "*.ino" | grep -q .; then echo "🎯 Arduino/Embedded"; fi
```

## Step 4: Coverage Validation (web projects only)

Validate >80% coverage per technology. Report:

```
📊 TEST COVERAGE SUMMARY
========================

🎯 NestJS Backend:
├── Unit Tests:        156 passed, 2 failed
├── Integration Tests: 45 passed
├── Coverage:          87.3% ✅ (>80% required)
└── Duration:          15.2s

🎯 Angular Frontend:
├── Unit Tests:        89 passed
├── Coverage:          91.2% ✅
└── Duration:          12.8s

🎯 Arduino/Embedded Firmware:
├── Compilation:       ✅ PASS
├── delay() calls:     ✅ None
├── Watchdog:          ✅ Enabled
├── Failsafe:          ✅ Present
├── Heap monitoring:   ✅ Present
└── Duration:          8.4s

📈 OVERALL RESULT: ✅ PASS
```

## Step 5: Failure Handling

- **Failed tests**: list file locations and test names
- **Low coverage**: identify modules below 80% (web)
- **Compilation errors**: display full error output
- **Missing production checks**: list each missing observability/safety item as a warning

## Notes
- Arduino projects have no automated unit tests — compilation + static + production checks is the validation gate
- Heap monitoring and reset reason logging are **warnings** on AVR (not applicable), **required** on ESP32
- Watchdog and failsafe checks are **required** for any project driving motors or actuators
