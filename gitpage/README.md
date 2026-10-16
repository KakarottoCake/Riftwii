<!-- SPDX-FileCopyrightText: 2026 RiftWii contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# RiftWii website

The GitHub Pages website is currently kept in the `gitpage/` directory for testing.

## Preview locally

The website can be previewed locally before publishing.

### Windows

From inside the `gitpage/` folder, run:

```text
open-site.bat
```

This starts the local web server and opens the website in your browser.

### Python

From inside the `gitpage/` folder, run:

```text
python serve.py
```

Then open the local address shown in the terminal, usually:

```text
http://localhost:8000
```

If port `8000` is already being used by another local server, stop the old server or change the port in `serve.py`.

To stop the local server, return to the terminal and press:

```text
Ctrl + C
```

## Tutorials

The tutorial list is controlled by:

```text
tutorials.json
```

To add a new tutorial:

1. Add the tutorial `.html` file to the `tutorials/` folder.
2. Add the tutorial filename to `tutorials.json`.

For example:

```json
[
    "getting-started.html",
    "installing-mods.html",
    "send-a-problem-report.html",
    "my-new-tutorial.html"
]
```

`tutorials.json` is the normal source used to build the tutorial list on the website.

If `tutorials.json` cannot be loaded, the site may use its built-in fallback tutorial list instead.