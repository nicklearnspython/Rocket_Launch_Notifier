# Test Instructions

Use the standard library `unittest` runner for this folder: `python -m unittest discover -s tests`. Write tests TDD-style in small vertical slices: one behavior-focused test through the public Watcher interface, minimal implementation to make it pass, then repeat.

Keep tests focused on observable behavior and project vocabulary, especially config defaults, validation failures, and CLI command surfaces; avoid coupling tests to private helpers or implementation shape, and only refactor after the suite is green.
