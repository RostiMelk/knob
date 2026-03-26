#!/usr/bin/env python3
"""One-time Spotify OAuth flow to get a refresh token.

Usage:
    python3 apps/spotify/scripts/get_token.py CLIENT_ID

Opens your browser to authorize, catches the callback on localhost:8888,
and prints the refresh token to paste into sdkconfig.defaults.local.
"""

import http.server
import sys
import urllib.parse
import webbrowser
import hashlib
import base64
import secrets
import json
import urllib.request

PORT = 8888
REDIRECT_URI = f"http://127.0.0.1:{PORT}/callback"
SCOPES = " ".join([
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-currently-playing",
    "user-library-read",
])


def base64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <CLIENT_ID>")
        sys.exit(1)

    client_id = sys.argv[1]

    # PKCE challenge
    verifier = base64url(secrets.token_bytes(32))
    challenge = base64url(hashlib.sha256(verifier.encode()).digest())

    # Open browser for auth
    auth_url = (
        "https://accounts.spotify.com/authorize?"
        + urllib.parse.urlencode({
            "client_id": client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT_URI,
            "scope": SCOPES,
            "code_challenge_method": "S256",
            "code_challenge": challenge,
        })
    )

    print(f"Opening browser for Spotify login...")
    webbrowser.open(auth_url)

    # Wait for callback
    auth_code = None

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            nonlocal auth_code
            query = urllib.parse.urlparse(self.path).query
            params = urllib.parse.parse_qs(query)

            if "code" in params:
                auth_code = params["code"][0]
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.end_headers()
                page = b"""<html><body style="margin:0;display:flex;align-items:center;
                    justify-content:center;height:100vh;font-family:system-ui;
                    text-align:center;background:#111">
                    <div>
                    <div style="position:relative;width:200px;height:200px;margin:0 auto 30px">
                      <!-- blue ridged ring -->
                      <div style="position:absolute;inset:0;border-radius:50%;
                        background:conic-gradient(from 0deg,#2266cc,#4499ff,#2266cc,#4499ff,
                        #2266cc,#4499ff,#2266cc,#4499ff,#2266cc,#4499ff,#2266cc,#4499ff,#2266cc);
                        box-shadow:0 0 40px rgba(68,136,255,0.3)"></div>
                      <!-- black bezel -->
                      <div style="position:absolute;inset:16px;border-radius:50%;
                        background:#000;box-shadow:inset 0 2px 8px rgba(0,0,0,0.8)"></div>
                      <!-- screen -->
                      <div style="position:absolute;inset:24px;border-radius:50%;
                        background:linear-gradient(180deg,#0a0a1a 0%,#1a1a3a 100%);
                        display:flex;align-items:center;justify-content:center;
                        flex-direction:column;gap:8px">
                        <!-- spotify icon -->
                        <svg width="48" height="48" viewBox="0 0 24 24" fill="#1DB954">
                          <path d="M12 0C5.4 0 0 5.4 0 12s5.4 12 12 12 12-5.4
                          12-12S18.66 0 12 0zm5.521 17.34c-.24.359-.66.48-1.021.24
                          -2.82-1.74-6.36-2.101-10.561-1.141-.418.122-.779-.179-.899
                          -.539-.12-.421.18-.78.54-.9 4.56-1.021 8.52-.6
                          11.64 1.32.42.18.479.659.301 1.02zm1.44-3.3c-.301.42
                          -.841.6-1.262.3-3.239-1.98-8.159-2.58-11.939-1.38-.479.12
                          -1.02-.12-1.14-.6-.12-.48.12-1.021.6-1.141C9.6 9.9 15
                          10.561 18.72 12.84c.361.181.54.78.241 1.2zm.12-3.36C15.24
                          8.4 8.82 8.16 5.16 9.301c-.6.179-1.2-.181-1.38-.721-.18-.601
                          .18-1.2.72-1.381 4.26-1.26 11.28-1.02 15.721 1.621.539.3
                          .719 1.02.419 1.56-.299.421-1.02.599-1.559.3z"/>
                        </svg>
                        <span style="color:#888;font-size:10px;letter-spacing:3px">
                          NOW PLAYING</span>
                      </div>
                    </div>
                    <h1 style="color:#fff;margin:0 0 8px">Your knob is connected to Spotify</h1>
                    <p style="font-size:1.3em;color:#1DB954;margin:0">Give it a spin.</p>
                    </div></body></html>"""
                self.wfile.write(page)
            else:
                self.send_response(400)
                self.send_header("Content-Type", "text/html")
                self.end_headers()
                error = params.get("error", ["unknown"])[0]
                self.wfile.write(f"<h1>Error: {error}</h1>".encode())

        def log_message(self, format, *args):
            pass  # silence logs

    server = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
    print(f"Waiting for callback on http://127.0.0.1:{PORT}/callback ...")
    server.handle_request()
    server.server_close()

    if not auth_code:
        print("No auth code received. Aborting.")
        sys.exit(1)

    # Exchange code for tokens
    print("Exchanging code for tokens...")
    data = urllib.parse.urlencode({
        "client_id": client_id,
        "grant_type": "authorization_code",
        "code": auth_code,
        "redirect_uri": REDIRECT_URI,
        "code_verifier": verifier,
    }).encode()

    req = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )

    with urllib.request.urlopen(req) as resp:
        tokens = json.loads(resp.read())

    refresh_token = tokens.get("refresh_token")
    if not refresh_token:
        print(f"Error: {tokens}")
        sys.exit(1)

    print()
    print("=== Your refresh token ===")
    print(refresh_token)
    print()
    print("Add this to apps/spotify/sdkconfig.defaults.local:")
    print(f'CONFIG_SPOTIFY_REFRESH_TOKEN="{refresh_token}"')


if __name__ == "__main__":
    main()
