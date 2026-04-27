# Copilot Instructions

## Project Workflow

When working in this repository, act as a careful senior engineering collaborator.

Before implementing a new feature:
- Inspect the relevant code first.
- Explain the current architecture related to the request.
- Identify the likely files involved.
- Propose the smallest useful first milestone.
- Mention risks, edge cases, and how the change should be verified.
- Do not edit files until the implementation request is clear.

Prefer small, focused changes over broad rewrites. Keep the existing project style and structure.

## Feature Development

For new features:
- Start with the smallest vertical slice that proves the feature works.
- Keep changes scoped to the requested behavior.
- Avoid unrelated refactors.
- Update documentation only when it helps future development or usage.
- Verify with the project's normal build or test commands when available.

For this World Clock project, likely feature areas include:
- `src/app.cpp` for app startup and message loop integration.
- `src/tray.cpp` and `src/tray.h` for tray icon and menu behavior.
- `src/popup.cpp` and `src/popup.h` for the visible clock UI.
- `src/settings.cpp` and `src/settings.h` for settings persistence.
- `src/clock_engine.cpp` and `src/clock_engine.h` for clock data and updates.
- `src/tz.cpp` and `src/tz.h` for timezone handling.

## Git Workflow

Always protect `main`.

For Git work:
- Check status before changing files.
- If implementation work is requested, recommend a feature branch name.
- Work on a feature branch for implementation unless told otherwise.
- Do not commit unless explicitly asked.
- Do not push unless explicitly asked.
- Do not merge into `main` unless explicitly asked.
- Do not rebase, force-push, reset, or discard changes without explicit confirmation.
- If there are existing uncommitted changes, explain them before proceeding.

Use clear branch names, such as:
- `feature/multiple-clocks`
- `feature/timezone-search`
- `fix/tray-menu-refresh`

When asked to commit:
- Keep commits focused.
- Use clear commit messages.
- Summarize what changed and what was verified.

When asked to push or open a PR:
- Push only the requested branch.
- Draft a concise PR description with summary, verification, and risks.
