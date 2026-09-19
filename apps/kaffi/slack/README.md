# Kaffi Slack app

Definition of the **Kaffi** Slack bot that posts scheduled coffee leaderboards. The
manifest is the source of truth; the bot token it yields is consumed by the Sanity
scheduled functions in `../blueprint` (see the kaffi README).

- App ID: `A0C2W442UES` in the `sanity-io` workspace (`.slack/apps.json`)
- Scopes: `chat:write` only. The bot posts to channels it has been invited to.
- No server: nothing runs here. There are no event subscriptions or interactivity.

## Commands

```bash
bun install
bunx slack manifest validate --team T02AAEL0P      # check manifest.json
bunx slack app install --team T02AAEL0P -E deployed # (re)install after manifest changes
bunx slack app settings                             # open app settings in the browser
```

Changing scopes means editing `manifest.json`, re-running `app install`, and
re-authorizing.

## Admin approval

The Sanity workspace requires admin approval for custom apps. The first install
attempt (2026-09-18) returned `app_approval_request_denied`. An admin approves the
app under Slack admin → Manage apps, or via the app's page at
<https://api.slack.com/apps/A0C2W442UES>.

## Bot token

After install, copy **Bot User OAuth Token** from the app's *OAuth & Permissions*
page and hand it to the function, never to the repo:

```bash
cd ../blueprint
bunx sanity functions env add <function-name> SLACK_BOT_TOKEN <xoxb-…>
```

Channel IDs live on `office` documents in the kaffi Sanity project, so adding an
office channel is: invite `@Kaffi` to the channel, paste the channel ID in the Studio.
