# Kaffi Slack app

Definition of the **Kaffi** Slack bot that posts scheduled coffee leaderboards. The
manifest is the source of truth; the bot token it yields is consumed by the Sanity
scheduled functions in `../blueprint` (see the kaffi README).

- The app and team IDs live in `.slack/apps.json`, which is gitignored. Bind a fresh
  checkout to the existing app with `bunx slack app link`.
- Scopes: `chat:write` only. The bot posts to channels it has been invited to.
- No server: nothing runs here. There are no event subscriptions or interactivity.

## Commands

```bash
bun install
bunx slack manifest validate --team <team-id>      # check manifest.json
bunx slack app install --team <team-id> -E deployed # (re)install after manifest changes
bunx slack app settings                             # open app settings in the browser
```

Changing scopes means editing `manifest.json`, re-running `app install`, and
re-authorizing.

## Admin approval

The workspace requires admin approval for custom apps; the first install attempt
returned `app_approval_request_denied`. An admin approves the app under Slack admin →
Manage apps, or from the app's page (`bunx slack app settings`).

## Bot token

After install, copy **Bot User OAuth Token** from the app's *OAuth & Permissions*
page and hand it to the function, never to the repo:

```bash
cd ../blueprint
bunx sanity functions env add <function-name> SLACK_BOT_TOKEN <xoxb-…>
```

Channel IDs live on `office` documents in the kaffi Sanity project, so adding an
office channel is: invite `@Kaffi` to the channel, paste the channel ID in the Studio.
