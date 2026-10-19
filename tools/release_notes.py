#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The release's two texts: the GitHub release notes (release/notes.md)
and the testers' Discord message (release/discord.md), filled in for one
build and checked before anything is published.

    python3 tools/release_notes.py render --version 2610-500 --dol riftwii.dol \\
        --repo KakarottoCake/Riftwii [--role-id 1234] [--require-role] --out out/
    python3 tools/release_notes.py post --dir out/ [--role-id 1234]

render writes out/notes.md, out/discord.md and out/discord-images/ (the
screenshots the message attaches). post sends out/discord.md and its images
to the webhook in the DISCORD_WEBHOOK_URL environment variable; the URL is
a secret, so it is never printed. The release workflow
(.github/workflows/release.yml) runs both.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

ROOT = pathlib.Path(__file__).resolve().parent.parent
NOTES = ROOT / "release" / "notes.md"
DISCORD = ROOT / "release" / "discord.md"
SCREENSHOTS = "docs/screenshots/"
VERSION = re.compile(r"^[0-9]{4}-[0-9]+$")
# The templates carry this line until someone writes the notes.
NOT_WRITTEN = "NOTES NOT WRITTEN YET"

# GitHub cuts a release body at 125000 characters. Discord refuses a
# message over 2000 characters, more than 10 attachments, or (on a server
# without boosts) more than 10 MB of files.
GITHUB_NOTES_LIMIT = 125000
DISCORD_TEXT_LIMIT = 2000
DISCORD_FILES_LIMIT = 10
DISCORD_BYTES_LIMIT = 10 * 1000 * 1000

# A line that is only a screenshot from the repository: sent as a file.
IMAGE_LINE = re.compile(r"^!\[[^\]]*\]\((" + re.escape(SCREENSHOTS) + r"[A-Za-z0-9_.-]+\.(?:png|jpg|jpeg|gif))\)$")
SCREENSHOT_LINK = re.compile(r"\{\{SCREENSHOTS\}\}([^)\s]*)")
WEBHOOK_HOSTS = ("discord.com", "discordapp.com", "ptb.discord.com", "canary.discord.com")


def fail(message):
    sys.exit("release_notes: " + message)


def tracked(path):
    """Committed files only: the notes load screenshots from the tag, so a
    screenshot that is only on this disk would show as a broken image."""
    result = subprocess.run(["git", "-C", str(ROOT), "ls-files", "--error-unmatch", "--", path], capture_output=True)
    return result.returncode == 0


def without_comments(template):
    return re.sub(r"<!--.*?-->\n?", "", template, flags=re.S)


def fill(template, values, source):
    """The template without its <!-- --> comments and with {{NAME}} filled
    in. Refuses an unwritten template and a name nobody fills."""
    text = without_comments(template)
    if NOT_WRITTEN in text:
        fail("%s still says %s: write this release's notes and delete that line" % (source, NOT_WRITTEN))
    for name, value in values.items():
        text = text.replace("{{%s}}" % name, value)
    left = re.findall(r"\{\{[A-Z_]+\}\}", text)
    if left:
        fail("%s has %s, which nothing fills in" % (source, ", ".join(sorted(set(left)))))
    return text.strip() + "\n"


def check_screenshot_links(template):
    for name in SCREENSHOT_LINK.findall(without_comments(template)):
        if not tracked(SCREENSHOTS + name):
            fail("release/notes.md shows %s%s, which is not committed" % (SCREENSHOTS, name))


def split_images(text):
    """(the message without its image lines, the screenshots to attach)."""
    kept = []
    images = []
    for line in text.split("\n"):
        match = IMAGE_LINE.match(line.strip())
        if match:
            images.append(match.group(1))
        else:
            kept.append(line)
    message = re.sub(r"\n{3,}", "\n\n", "\n".join(kept)).strip()
    return message, images


def render(args):
    if not VERSION.match(args.version):
        fail("%r is not a YYMM-N version" % args.version)
    dol = args.dol.read_bytes()
    tag = "v" + args.version
    server = "https://github.com/" + args.repo
    if args.role_id:
        if not re.fullmatch(r"[0-9]{5,25}", args.role_id):
            fail("the tester role ID should be a Discord ID (digits only)")
        tester_role = "<@&%s>" % args.role_id
    elif args.require_role:
        fail("no tester role ID: set the DISCORD_TESTER_ROLE_ID variable (see docs/DEVELOPING.md)")
    else:
        # A dry run without the variable still shows the message.
        tester_role = "@RiftWii Tester"
    values = {
        "VERSION": args.version,
        "TAG": tag,
        "DOL_SHA256": hashlib.sha256(dol).hexdigest().upper(),
        "DOL_BYTES": str(len(dol)),
        "SCREENSHOTS": "https://raw.githubusercontent.com/%s/%s/%s" % (args.repo, tag, SCREENSHOTS),
        "RELEASE_URL": "%s/releases/tag/%s" % (server, tag),
        "TESTER_ROLE": tester_role,
    }

    notes_template = NOTES.read_text(encoding="utf-8")
    notes = fill(notes_template, values, "release/notes.md")
    check_screenshot_links(notes_template)
    if len(notes) > GITHUB_NOTES_LIMIT:
        fail("release/notes.md is %d characters; GitHub keeps %d" % (len(notes), GITHUB_NOTES_LIMIT))

    message, images = split_images(fill(DISCORD.read_text(encoding="utf-8"), values, "release/discord.md"))
    if len(message) > DISCORD_TEXT_LIMIT:
        fail("the Discord message is %d characters; Discord takes %d" % (len(message), DISCORD_TEXT_LIMIT))
    if len(images) > DISCORD_FILES_LIMIT:
        fail("the Discord message has %d images; Discord takes %d" % (len(images), DISCORD_FILES_LIMIT))
    image_bytes = 0
    for image in images:
        if not tracked(image):
            fail("release/discord.md attaches %s, which is not committed" % image)
        image_bytes += (ROOT / image).stat().st_size
    if image_bytes > DISCORD_BYTES_LIMIT:
        fail("the Discord images are %d bytes together; Discord takes %d" % (image_bytes, DISCORD_BYTES_LIMIT))

    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    (out / "notes.md").write_text(notes, encoding="utf-8")
    (out / "discord.md").write_text(message + "\n", encoding="utf-8")
    image_dir = out / "discord-images"
    shutil.rmtree(image_dir, ignore_errors=True)
    image_dir.mkdir()
    # Numbered, so the post keeps the order the message gives them.
    for number, image in enumerate(images, 1):
        shutil.copyfile(ROOT / image, image_dir / ("%02d-%s" % (number, pathlib.PurePosixPath(image).name)))
    print("release notes: %d characters; Discord: %d characters and %d images (%d bytes)"
          % (len(notes), len(message), len(images), image_bytes))


def webhook_url():
    url = os.environ.get("DISCORD_WEBHOOK_URL", "").strip()
    if not url:
        fail("DISCORD_WEBHOOK_URL is empty: add the DISCORD_RELEASE_WEBHOOK secret (see docs/DEVELOPING.md)")
    parts = urllib.parse.urlsplit(url)
    if parts.scheme != "https" or parts.hostname not in WEBHOOK_HOSTS or not parts.path.startswith("/api/webhooks/"):
        # Not the URL itself: it is a secret, and a log is public.
        fail("DISCORD_WEBHOOK_URL is not a Discord webhook address")
    return url


def multipart(payload, files):
    """A multipart/form-data body: payload_json, then files[0], files[1]..."""
    boundary = "riftwii-" + uuid.uuid4().hex
    parts = []
    parts.append(("--%s\r\nContent-Disposition: form-data; name=\"payload_json\"\r\n"
                  "Content-Type: application/json\r\n\r\n" % boundary).encode())
    parts.append(json.dumps(payload).encode("utf-8") + b"\r\n")
    for number, path in enumerate(files):
        content_type = "image/png" if path.suffix == ".png" else "application/octet-stream"
        parts.append(("--%s\r\nContent-Disposition: form-data; name=\"files[%d]\"; filename=\"%s\"\r\n"
                      "Content-Type: %s\r\n\r\n" % (boundary, number, path.name, content_type)).encode())
        parts.append(path.read_bytes() + b"\r\n")
    parts.append(("--%s--\r\n" % boundary).encode())
    return b"".join(parts), "multipart/form-data; boundary=" + boundary


def post(args):
    url = webhook_url()
    if args.role_id and not re.fullmatch(r"[0-9]{5,25}", args.role_id):
        fail("the tester role ID should be a Discord ID (digits only)")
    message = (args.dir / "discord.md").read_text(encoding="utf-8").strip()
    files = sorted((args.dir / "discord-images").glob("*"))
    # Only the tester role may be pinged: an @everyone or a stray <@id> in
    # the text stays plain text.
    roles = [args.role_id] if args.role_id else []
    payload = {
        "content": message,
        "allowed_mentions": {"parse": [], "roles": roles},
        "attachments": [{"id": number, "filename": path.name} for number, path in enumerate(files)],
    }
    body, content_type = multipart(payload, files)
    # wait=true: Discord answers with the message, so a refusal shows here.
    request_url = url + ("&" if "?" in url else "?") + "wait=true"
    for attempt in range(3):
        request = urllib.request.Request(request_url, data=body, method="POST", headers={
            "Content-Type": content_type,
            # Discord refuses requests without a User-Agent of their own.
            "User-Agent": "RiftWii release workflow (https://github.com/KakarottoCake/Riftwii, 1)",
        })
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                answer = json.load(response)
            print("Discord: posted message %s with %d images" % (answer.get("id", "?"), len(files)))
            return
        except urllib.error.HTTPError as error:
            details = error.read(2000).decode("utf-8", "replace")
            if error.code == 429 and attempt < 2:
                # Rate limited: Discord says how long to wait.
                try:
                    wait = float(json.loads(details).get("retry_after", 5))
                except ValueError:
                    wait = 5
                time.sleep(min(wait, 60))
                continue
            fail("Discord refused the message (HTTP %d): %s" % (error.code, details))
        except urllib.error.URLError as error:
            fail("could not reach Discord: %s" % error.reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    render_parser = commands.add_parser("render", help="fill in and check the notes and the Discord message")
    render_parser.add_argument("--version", required=True)
    render_parser.add_argument("--dol", required=True, type=pathlib.Path)
    render_parser.add_argument("--repo", default="KakarottoCake/Riftwii", help="owner/name on GitHub")
    render_parser.add_argument("--role-id", default="", help="the Discord tester role's ID")
    render_parser.add_argument("--require-role", action="store_true", help="refuse without a role ID")
    render_parser.add_argument("--out", required=True, type=pathlib.Path)
    post_parser = commands.add_parser("post", help="send the rendered Discord message")
    post_parser.add_argument("--dir", required=True, type=pathlib.Path)
    post_parser.add_argument("--role-id", default="")
    args = parser.parse_args()
    if args.command == "render":
        render(args)
    else:
        post(args)


if __name__ == "__main__":
    main()
