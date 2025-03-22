---
sidebar_position: 3
sidebar_label: Code Commit Specification
---

# Code Commit Specification

## Commit Format

```plaintext
subject: message
```

## Subject Definitions

### feat: Add a new feature

Used for submitting new features.

Example: <u>feat: Add xxx feature</u>

### fix: Fix a bug

Used for submitting bug fixes.

Example: <u>fix: Fix xxx bug</u>

### docs: Documentation changes

Used for submitting changes related only to documentation.

Example: <u>docs: Update README.md</u>

### style: Code style changes (does not affect logic)

Used for submitting changes that only involve formatting, punctuation, whitespace, etc., and do not affect code execution.

Example: <u>style: Remove extra blank lines</u>

### refactor: Code refactoring

Used for submitting code refactoring.

Example: <u>refactor: Refactor List implementation logic</u>

### perf: Performance optimization

Used for submitting code changes that improve performance.

Example: <u>perf: Optimize image loading speed</u>

### test: Add or modify tests

Used for submitting test-related content.

Example: <u>test: Add unit tests for xxx module</u>

### chore: Miscellaneous (build process or auxiliary tools changes)

Used for submitting changes related to the build process, auxiliary tools, etc.

Example: <u>chore: Update dependency libraries</u>

### build: Build system or external dependency changes

Used for submitting changes that affect the build system.

Example: <u>build: Upgrade build script dependency versions</u>

### ci: Continuous integration configuration changes

Used for submitting changes to CI configuration files and scripts.

Example: <u>ci: Modify CI/CD configuration files</u>

### revert: Revert a commit

Used for reverting a previous commit.

Example: <u>revert: feat: Add xxx feature</u>

## Special Notes for Non-Native English Speakers

:::warning

Unless necessary, avoid using Non-English characters in commit messages and try to use English instead.

:::

- When mixing Non-English and English characters (e.g., filenames), add spaces. If one side is a punctuation mark, no space is needed.
  Example:

  ```plaintext
  docs: Update README.md file
  fix: Fix xxx issue
  ```

- Minimize the unnecessary use of the word `的`.
- Pay attention to the difference between Chinese and English punctuation: Chinese colon `：`, English colon `:`, Chinese comma `，`, English comma `,`.
- Do not add a period `。` or full stop `.` at the end.
- Wrap keywords with ` `` `.
