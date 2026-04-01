# Handle PR Review Feedback Loop

## Input
PR number: $ARGUMENTS

## Step 1: Check PR Status
1. **Get Comprehensive PR Details**:
   ```bash
   gh pr view $ARGUMENTS --json reviews,comments,state,statusCheckRollup,mergeable,url,headRefName
   ```

2. **Analyze Current State**:
   - **PR Status**: Open, Closed, Merged, Draft
   - **Review Status**: Approved, Changes Requested, Pending
   - **CI/CD Status**: Success, Failure, Pending
   - **Merge Conflicts**: Present or Clean
   - **Comments**: Review comments, suggestions, and requested changes

3. **Determine Action Required**:
   - ✅ **Ready to Merge**: All approvals + CI green + no conflicts
   - 🔄 **Needs Fixes**: Review comments or failing CI
   - ⏳ **Waiting**: Pending reviews or CI checks
   - ❌ **Blocked**: Merge conflicts or critical failures

## Step 2: Categorize Feedback

### 🔧 **Code Changes Required**
- Logic fixes or improvements
- Performance optimizations
- Security vulnerabilities
- Code style and formatting
- Architecture or design pattern issues
- **Embedded-specific**: `delay()` introduced (must be replaced with `millis()`), `String` class on AVR, ISR doing too much work, blocking call in `loop()`

### 📝 **Documentation Updates**
- Missing or incomplete documentation
- API documentation updates
- README or CLAUDE.md changes (e.g. new pins, commands, or constants added)
- Code comments and inline documentation

### 🧪 **Testing Requirements** (web projects)
- Missing test cases
- Test coverage improvements
- Integration test additions
- Mock or fixture updates

### 🏗️ **Build / Compile Issues**
- **Web**: Build failures, linting errors, type checking issues, dependency conflicts
- **Embedded**: Compilation errors — verify with `arduino-cli compile --fqbn <FQBN> <sketch_dir>` or Arduino IDE Verify (Ctrl+R)

## Step 3: Create Implementation Plan
For each feedback item:

1. **Assess Impact**: Determine scope and complexity
2. **Prioritize**: Order by importance and dependencies
3. **Technology Selection**: Choose appropriate agent:
   - **Backend Changes**: Use `nestjs-backend-architect` or `laravel-backend-architect`
   - **Frontend Changes**: Use `angular-frontend-developer` or `flutter-frontend-developer`
   - **Embedded Changes**: Use `arduino-embedded-developer` for architectural changes (new state machine state, protocol change, timing redesign); implement directly for straightforward fixes

## Step 4: Implement Changes

### Code Implementation
1. **Make Changes**: Implement the requested modifications following stack-specific conventions

2. **Validate**:
   - *Web*: Run tests (`npm test` / `php artisan test` / `flutter test`) and ensure >80% coverage
   - *Embedded*:
     ```bash
     SKETCH=$(find . -name "*.ino" -maxdepth 2 | head -1)
     arduino-cli compile --fqbn <FQBN> "$(dirname $SKETCH)" 2>&1
     grep -rn "delay(" . --include="*.ino" --include="*.cpp"
     ```

3. **Update Documentation**: CLAUDE.md if pins, commands, or key constants changed

## Step 5: Commit and Push Updates
```bash
git add -p
git commit -m "fix: address PR feedback - <specific change description>"
git push origin <branch-name>
```

## Step 6: Respond to Reviewers
```bash
gh pr comment $ARGUMENTS --body "✅ All feedback addressed. Ready for re-review:

**Changes Made:**
- [List specific changes]

**Verification:**
- ✅ Tests passing / Compilation passing
- ✅ Build successful
- ✅ No delay() calls (embedded) / Linting clean (web)

Please re-review when ready. Thanks!"
```

## Step 7: Feedback Resolution Loop

### 🔄 **If Changes Requested or CI/Compile Failing:**
1. Re-run planning: `explore-plan` with feedback context
2. Update implementation: `start-working-on-branch-new <branch-name>`
3. Re-validate: `run-tests`
4. Push updates — automatically updates PR
5. Repeat: re-run `update-feedback <pr-number>` until resolved

### ⏳ **If Waiting for Reviews:**
- Monitor PR status
- Notify reviewers if needed

### ✅ **If Ready to Merge:**
- Proceed to merge

## Step 8: Completion Criteria
Loop continues until ALL criteria met:
- [ ] ✅ **PR Approved**: At least 1 reviewer approved
- [ ] ✅ **Build Green**: All automated checks passing (web) / compilation passing (embedded)
- [ ] ✅ **No Conflicts**: Clean merge with main branch
- [ ] ✅ **All Feedback Addressed**: No outstanding review comments

## Step 9: Final Merge Process
Once all criteria satisfied:
1. **Merge PR**: `gh pr merge <pr-number> --squash`
2. **Delete Branch**: `git branch -d <branch-name>`
3. **Update Issue**: Mark GitHub issue as completed
4. **Clean Session**: Archive session file
