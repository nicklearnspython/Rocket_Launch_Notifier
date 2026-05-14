# Issue tracker: GitHub

Issues and PRDs for this repo live as GitHub issues in `nicklearnspython/Rocket_Launch_Notifier`.

Use the connected GitHub app when available. If using the GitHub CLI, run commands from this clone so the repo can be inferred from `git remote -v`.

## Conventions

- Create an issue: `gh issue create --title "..." --body "..."`
- Read an issue: `gh issue view <number> --comments`
- List issues: `gh issue list --state open`
- Comment on an issue: `gh issue comment <number> --body "..."`
- Apply or remove labels: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- Close an issue: `gh issue close <number> --comment "..."`

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Fetch the matching GitHub issue and its comments.
