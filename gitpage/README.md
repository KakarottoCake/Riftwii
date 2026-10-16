<!-- SPDX-FileCopyrightText: 2026 RiftWii contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# RiftWii website

The GitHub Pages website is currently kept in the `gitpage/` directory for testing.

## Preview locally

From `gitpage/`:

```text
python -m http.server
```

## Add a tutorial

1. Add the `.html` file to `gitpage/tutorials/`.
2. Add its filename to `gitpage/tutorials.json`.
3. Reuse suitable current screenshots from `docs/screenshots/` where they match the tutorial.
4. Put tutorial-only media in `gitpage/tutorials/resources/`.
5. Preview the site before committing.

`tutorials.json` is the normal tutorial index. The GitHub API is only a fallback.
