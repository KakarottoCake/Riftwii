# Releasing RiftWii

How a RiftWii release is made with the release workflow
(`.github/workflows/release.yml`): what is set up once, what is done for
every release, how the workflow works inside, what it refuses, and what to
do when a step fails. [DEVELOPING.md](DEVELOPING.md#releasing) has the
short version and the steps for a release packed by hand.

This page is written so a person or an AI session can follow it without
other context. An AI session reads "Rules for AI sessions" first.

## Rules for AI sessions

- **Never publish on your own.** Running the workflow in `publish` mode,
  creating a tag, a GitHub release or a Discord post is the maintainer's
  decision every time. An agent may write the notes, commit them, and, if
  the maintainer asks, start a `dry-run`.
- **Never put the Discord webhook URL in the repository**, in a commit
  message, a log line, an issue or a PR. It lives only in the
  `DISCORD_RELEASE_WEBHOOK` secret. `tools/ci_rules.py` fails a commit that
  contains one. If a webhook URL appears in a conversation, say that it
  should be regenerated, and don't repeat it.
- **Never make a GitHub pre-release** (see "Beta, stable and test builds").
- Follow [AGENTS.md](../AGENTS.md) for commits: commit as `riftwii`, add
  only the files you changed, no AI attribution lines.

## Release kinds: one kind only

Since 2610-432 there is **no Stable or Beta**. Every release is for every
player:

- A release is published as GitHub's **latest** release, never as a
  pre-release. Every RiftWii's update check reads `/releases/latest`, so
  every player is offered it (Settings > Check for a new version).
- Older builds still have a Stable/Beta setting. On Beta they take the
  newest release of *all*, pre-releases included. A GitHub pre-release
  would therefore reach those players as an update. That's why a test
  build is never a pre-release.
- **A test build for a few testers** is a dry run's files, posted by hand
  in the testers' Discord channel (see "A test build" below).

## Versions

The version is `YYMM-N`, for example `2610-489`:

- `YYMM` is the release line, `project(Riftwii VERSION 2610 ...)` in
  `CMakeLists.txt`.
- `N` is the number of commits since `RIFTWII_BASE_TAG` (`v2.7.0`, also in
  `CMakeLists.txt`), counted by git at build time.
- `make -f Makefile.wii version` prints the version the checked-out
  commit builds as, without building.
- The tag is `v` plus the version (`v2610-489`); the release is titled
  `RiftWii 2610-489`; the zip is `riftwii-v2610-489.zip`.

Every commit has its own number, so a commit can be released only once.
To release again, push a new commit (the next release's notes are one).

## Files involved

| Path | What it is |
| --- | --- |
| `.github/workflows/release.yml` | The workflow: jobs `check`, `build`, `publish` |
| `release/notes.md` | The GitHub release notes for the next release, with `{{...}}` names the workflow fills in |
| `release/discord.md` | The testers' Discord message for the next release |
| `release/zip/` | Text files that go into the zip as they are laid out here: `README.txt` (the players' README with its changelog), `SOURCE.txt` (where the source is, GPL), the `PUT_..._HERE.txt` folder notes, `sd-card/riftwii/themes/README.txt` |
| `tools/make_release_zip.py` | Packs the zip |
| `tools/make_update_pack.py` | Packs `riftwii-update.pack` from the zip and `themes/` |
| `tools/release_notes.py` | `render`: fills in and checks the two texts. `post`: sends the Discord message |
| `docs/screenshots/` | Screenshots for the notes and the Discord message (committed before the release) |
| `hbc/meta.xml`, `hbc/icon.png`, `hbc/music.ogg` | The Homebrew Channel files; the zip's `meta.xml` gets the release's version and date |
| `channel/installer/hbc/` | The channel installer's `meta.xml` and `icon.png` |

## One-time setup

1. **The workflow is on main.** GitHub lists a manual workflow under
   Actions only once its file is on the default branch.
2. **A Discord webhook for the testers' channel.** In Discord: the
   channel's Edit Channel > Integrations > Webhooks > New Webhook, then
   Copy Webhook URL. A webhook URL that was ever pasted somewhere public
   or into a chat should be deleted and made again.
3. **The secret.** GitHub: Settings > Secrets and variables > Actions >
   Secrets > New repository secret. Name `DISCORD_RELEASE_WEBHOOK`, value
   the webhook URL.
4. **The tester role's ID.** Discord: User Settings > Advanced >
   Developer Mode on; then Server Settings > Roles, right-click
   "RiftWii Tester" > Copy Role ID (a long number). GitHub: Settings >
   Secrets and variables > Actions > Variables > New repository variable.
   Name `DISCORD_TESTER_ROLE_ID`, value the ID. Without it the message
   can't ping the role, so a publish that posts to Discord refuses to run.
5. **Optional: an approval before each publish.** The `publish` job runs
   in the `release` environment, which GitHub creates on its first use.
   Settings > Environments > `release` > Required reviewers adds a
   "Review deployments" button that must be pressed before anything is
   published. The secret can be moved into that environment so no other
   workflow can read it.

## Every release

### 1. Write the texts

Both files start with a `<!-- -->` comment that explains them. The comment
is dropped when the files are filled in. Both also carry a line
`NOTES NOT WRITTEN YET`: delete it once the text is written, because the
workflow refuses a file that still has it.

**Only what a player notices goes in either text.** A change belongs in
the notes when it changes what RiftWii does on a Wii or in Dolphin: the
menu, a launch, a game, saves, controllers, the logs and problem reports a
player sends, the files in the download. Leave out everything that only
changes how RiftWii is made: CI and other GitHub Actions workflows (this
release workflow included), review bots such as the CodeRabbit config,
`tools/` scripts, tests, docs, refactors with no visible effect and
changes to the release process. Commit subjects describe all of these
the same way, so read what each commit changes rather than listing them
all. When none is left, there is nothing to release yet.

**`release/notes.md`**, the GitHub release notes. Keep the structure of
the earlier releases:

1. The intro: `**RiftWii {{VERSION}}.** <one sentence>. Every RiftWii offers this update by itself (Settings > Check for a new version).`
2. A screenshot or two: `![What it shows]({{SCREENSHOTS}}2610-500-name.png)`.
3. `## What changed`: bullets, each starting with a bold phrase of what a
   player notices, then why. Say plainly when a fix couldn't be tested on
   a Wii.
4. `## What to test`: a numbered list for the testers.
5. `## Known issues`: carried over from the last release, updated.
6. The `Everything new since 2.7.0`, `## Download` and
   `## License and source` sections stay as they are. They already use
   `{{TAG}}`, `{{DOL_SHA256}}` and `{{DOL_BYTES}}`.

**`release/discord.md`**, the testers' message, in Discord markdown. The
two earlier messages had this shape:

```
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

<a heading line, e.g. "Fixes for 2610-454:" or "GameCube, a lot of it:">

* change one
* change two

To test, please:

1. first thing
2. second thing

Sending logs (you don't need to take out your SD card anymore): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![The new menu](docs/screenshots/2610-500-menu.png)
```

A line that is only `![...](docs/screenshots/<file>.png)` (or `.jpg`,
`.gif`) is taken out of the text and attached under the message, in order.
Limits: 2000 characters of text once filled in, 10 pictures, 10 MB of
pictures in all.

Names the workflow fills in, in either file:

| Name | Becomes |
| --- | --- |
| `{{VERSION}}` | `2610-500` |
| `{{TAG}}` | `v2610-500` |
| `{{DOL_SHA256}}` | the built `riftwii.dol`'s SHA-256, upper case |
| `{{DOL_BYTES}}` | its size in bytes |
| `{{SCREENSHOTS}}` | `https://raw.githubusercontent.com/KakarottoCake/Riftwii/v2610-500/docs/screenshots/` |
| `{{RELEASE_URL}}` | `https://github.com/KakarottoCake/Riftwii/releases/tag/v2610-500` |
| `{{TESTER_ROLE}}` | `<@&ROLE_ID>`, which pings the tester role (`@RiftWii Tester` as plain text in a dry run without the variable) |

Any other `{{NAME}}` left in a file fails the run.

### 2. Screenshots and the zip's README

- Put the screenshots in `docs/screenshots/`, named `YYMM-N-what.png`
  (the number can be a guess; it's only a name). They must be
  committed: the notes load them from the tag, and the workflow refuses a
  picture git doesn't track.
- Add the release's line to the changelog in `release/zip/README.txt`
  (the `- 2610-489: ...` lines). Its first line, `RiftWii {{VERSION}}`, is
  filled in by the zip tool.
- When the release line changes (a new `YYMM`), bump it in
  `CMakeLists.txt` as DEVELOPING.md step 1 says. The zip's `meta.xml`
  gets the version and date by itself.

### 3. Push and wait for CI

Commit the texts, the screenshots and the README change, push them to
main, and wait for CI (`.github/workflows/ci.yml`) to pass on that
commit. A publish refuses a commit whose CI hasn't passed. A dry run only
warns.

### 4. Dry run

GitHub: Actions > Release > Run workflow. Branch `main`, mode
**dry-run**, Run workflow.

When it finishes, open the run:

- **Summary**: the version and commit, every file's SHA-256, the Discord
  message exactly as it will be sent, the names of the pictures it
  attaches, and the release notes rendered as GitHub will show them.
- **Artifacts**: `riftwii-vYYMM-N` holds `out/` (the zip, `riftwii.dol`,
  `riftwii-update.pack`) and `texts/` (`notes.md`, `discord.md`,
  `discord-images/`). `riftwii-vYYMM-N-elf` holds `riftwii.elf` and the
  plugins' ELFs, which turn a crash's offsets into names.

Download the artifact and check the build in Dolphin
([DEVELOPING.md](DEVELOPING.md), [HARNESS.md](HARNESS.md)). This is the
DOL that will be published: the workflow builds with CI's pinned devkitPPC
image, so it is not byte for byte a local build.

If something is wrong, fix it, push, and dry-run again.

### 5. Publish

Actions > Release > Run workflow. Branch `main`, mode **publish**,
"Post release/discord.md" ticked (untick it to publish without the
Discord message). If the `release` environment asks for an approval,
press Review deployments > Approve.

The workflow then:

1. builds the commit again, as in the dry run;
2. creates the tag `vYYMM-N` on that commit and the release
   "RiftWii YYMM-N", marked latest, with `riftwii-vYYMM-N.zip`,
   `riftwii.dol` and `riftwii-update.pack`, and the filled-in notes;
3. posts the Discord message with its pictures, pinging only the tester
   role.

Check the release page (linked in the run's summary) and the Discord
channel.

## A test build

For something only a few testers should try (a fix to confirm on a real
Wii, an experiment):

1. Write the texts if you want them (a dry run needs them written), push,
   and run a **dry-run**.
2. Download the artifact. Send the testers `out/riftwii-vYYMM-N.zip` (the
   whole zip, because a DOL needs the plugins of its own build:
   `sd-card/apps/riftwii/plugins/*.rwp`).
3. Never publish it as a GitHub release or pre-release.

## What the workflow checks, and the errors you can get

| Message | Why, and what to do |
| --- | --- |
| `A release is published from main` | Publish was run on another branch. Run it on main. |
| `CI on <sha> is '<state>', not success` | CI is still running or failed on that commit. Wait, or fix it. |
| `release/notes.md is the same as at v...` | The texts weren't rewritten since the last release. Write them. |
| `... still says NOTES NOT WRITTEN YET` | Delete that line once the text is written. |
| `... has {{NAME}}, which nothing fills in` | A misspelt or unknown name. Use one from the table above. |
| `... shows docs/screenshots/x.png, which is not committed` | Commit the picture, or fix its name. |
| `the Discord message is N characters; Discord takes 2000` | Shorten `release/discord.md`. |
| `... images; Discord takes 10` / `... bytes together` | Fewer or smaller pictures. |
| `no tester role ID` | Set the `DISCORD_TESTER_ROLE_ID` variable, or untick Discord. |
| `vYYMM-N is already a tag` | That commit was released. Push a new commit. |
| `no gamecube.YYMM-N.rwp in ...` / `is from another version` | The plugins don't match the build; a build problem, look at the build step's log. |
| `make_update_pack: ...` | The update pack refused the zip (usually a plugin that doesn't match the DOL). |
| `DISCORD_WEBHOOK_URL is empty` | The `DISCORD_RELEASE_WEBHOOK` secret is missing (or is in an environment the job doesn't use). |
| `Discord refused the message (HTTP 4xx)` | A deleted or wrong webhook (401/404), or a message Discord didn't accept; its reply is printed. |

Every check except the Discord post runs before anything is published.

**When only the Discord post fails**, the release is already up. Fix the
cause (usually the secret), open the failed run and press **Re-run failed
jobs**: the publish job sees the release already made from that commit,
skips it, and posts the message. To post nothing, do nothing: the release
stands on its own. The message text is also in the artifact
(`texts/discord.md`) to paste by hand.

**When the Discord post went out wrong** (for example `@unknown-role`
because `DISCORD_TESTER_ROLE_ID` held a wrong ID), re-running the publish
job doesn't help: the role was filled into the text when the release was
built, and a new build stops at the existing tag. Fix the cause, delete the
wrong post in Discord, then run Actions > Release > Run workflow on main
with mode **discord-repost** and the release's version (`2610-541`). It
fills in that tag's `release/discord.md` again with the role as it is set
now and posts it, with its pictures. It builds and publishes nothing.

**A wrong release** can be fixed on GitHub: edit its notes, or delete the
release *and* its tag and publish again from a new commit. Players who
already updated keep that build, so prefer a new release with the fix.

## How the workflow works

`.github/workflows/release.yml`, started only by hand
(`workflow_dispatch`), with three inputs: `mode` (`dry-run`, `publish` or
`discord-repost`), `discord` (true or false) and `version` (only for
`discord-repost`). Runs never overlap (`concurrency: release`).
The workflow's token is read-only except in the publish job.

**Job `check`** (Ubuntu): refuses a publish not on main; asks the GitHub
API for `ci.yml`'s run on this commit and refuses a publish unless it
succeeded; compares `release/notes.md` and `release/discord.md` with the
latest `vYYMM-*` tag reachable from the commit and refuses them if they
are unchanged.

**Job `build`** (the same pinned `devkitpro/devkitppc` image as CI's Wii
build, full history and tags):

1. `make -f Makefile.wii version` gives the version; a tag of that name
   already existing stops the run.
2. `tools/release_notes.py render` on a stand-in DOL, so a mistake in the
   texts fails in seconds instead of after the build.
3. `make -f Makefile.wii` (the DOL and its plugins), the plugins checked
   against the DOL with `tools/make_plugin.py check`, and
   `make -f Makefile.channel` (the channel installer).
4. `tools/make_release_zip.py` packs the zip:
   - `sd-card/apps/riftwii/`: `boot.dol` (the built `riftwii.dol`),
     `meta.xml` (`hbc/meta.xml` with the version and the commit's date),
     `icon.png`, `music.ogg`, `plugins/` (every `<name>.<version>.rwp`;
     `gamecube` is required and another version's plugin is refused);
   - `sd-card/apps/riftwii_channel/`: `boot.dol` (`build-channel/installer.dol`),
     `meta.xml`, `icon.png`;
   - `sd-card/riftwii/themes/`: every committed theme in `themes/` except
     the kit;
   - everything in `release/zip/`, with `{{VERSION}}`, `{{TAG}}`,
     `{{COMMIT}}` and `{{REPO_URL}}` filled in;
   - `LICENSE.txt` (the repository's `LICENSE`) and `NOTICE.md`.

   Text files get CRLF line ends for Notepad. Every entry carries the
   commit's time, so the same commit gives the same zip. It refuses to
   run with uncommitted changes in the files it packs.
5. `tools/make_update_pack.py` makes `riftwii-update.pack` from the zip.
   The zip is listed to show its plugins.
6. `tools/release_notes.py render` on the real DOL writes `texts/`.
7. The summary, and the two artifacts.

**Job `publish`** (Ubuntu, only in `publish` mode, environment `release`,
`contents: write`): downloads the artifact; if the release exists and its
tag is this commit, goes on, otherwise refuses an existing one; runs
`gh release create vYYMM-N --target <commit> --title "RiftWii YYMM-N"
--notes-file texts/notes.md --latest` with the three files; then, if
`discord` is ticked, `tools/release_notes.py post`.

**Job `repost`** (Ubuntu, only in `discord-repost` mode, environment
`release`, read-only token; `check`, `build` and `publish` are skipped):
refuses a version that isn't a published release, checks out its tag,
downloads its `riftwii.dol`, runs `release_notes.py render` with the
tester role as it is set now (`--require-role`) and `post`.

**The Discord post** (`release_notes.py post`): reads the webhook from
the `DISCORD_WEBHOOK_URL` environment variable (the secret) and checks it
is an `https` Discord webhook address, never printing it. It sends one
`multipart/form-data` request: `payload_json` with the text,
`allowed_mentions` limited to the tester role (an `@everyone` or another
mention in the text stays plain text), and the pictures as `files[0..9]`.
With `?wait=true`, Discord's answer shows in the log. A rate limit
(HTTP 429) is waited out twice.

## Running the steps by hand

The tools work outside the workflow, for example to check the texts
before pushing (in devkitPro's MSYS2 shell on Windows, as AGENTS.md
describes):

```bash
make -f Makefile.wii version                     # the YYMM-N this commit builds as
py -3.13 tools/release_notes.py render --version YYMM-N --dol riftwii.dol --out texts
py -3.13 tools/make_release_zip.py --version YYMM-N --dol riftwii.dol \
    --channel build-channel/installer.dol --plugins build-plugins/plugins \
    --out riftwii-vYYMM-N.zip
py -3.13 tools/make_update_pack.py riftwii-vYYMM-N.zip riftwii-update.pack
```

`render` checks everything the workflow checks about the texts and
writes `texts/notes.md`, `texts/discord.md` and `texts/discord-images/`.
Don't run `post` by hand: it posts to the testers' channel.

## Per-release checklist

- [ ] `release/notes.md` written, `NOTES NOT WRITTEN YET` deleted
- [ ] `release/discord.md` written, `NOTES NOT WRITTEN YET` deleted
- [ ] Screenshots committed in `docs/screenshots/`
- [ ] Changelog line added to `release/zip/README.txt`
- [ ] Pushed to main, CI green
- [ ] Dry run: summary read, artifact tried in Dolphin
- [ ] Publish (the maintainer)
- [ ] Release page and Discord post checked
