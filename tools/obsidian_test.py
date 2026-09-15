#!/usr/bin/env python3
# =============================================================================
# OBSIDIAN LOCAL REST API REQUEST TESTER
# =============================================================================
# Writes a note to the vault with the same route, headers, and body
# main/net/obsidian_client.cpp sends for the daily and note modes, for
# debugging from a desktop instead of from the board.
#
# Usage: obsidian_test.py "note text"
# Env:   OBS_URL, OBS_KEY, OBS_MODE, OBS_FOLDER, OBS_LINE (defaults match
#        main/app/settings.cpp)

import os
import ssl
import sys
import time
import urllib.error
import urllib.request

OBS_URL = os.environ.get("OBS_URL", "http://192.168.1.2:27123")
OBS_KEY = os.environ.get("OBS_KEY", "")
OBS_MODE = os.environ.get("OBS_MODE", "daily")
OBS_FOLDER = os.environ.get("OBS_FOLDER", "Inbox")
OBS_LINE = os.environ.get("OBS_LINE", "- **{time}** {text}")

# Characters encode_path in obsidian_client.cpp leaves alone, slash included
# so folder separators survive.
SAFE = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
           "/-_.~")


# VAULT PATH ENCODER
# Percent-encodes a vault path one UTF-8 byte at a time, keeping slashes as
# separators.
def encode_path(path):
    out = []
    for byte in path.encode("utf-8"):
        char = chr(byte)
        out.append(char if char in SAFE else f"%{byte:02X}")
    return "".join(out)


# BASE URL NORMALIZER
# Strips trailing slashes and reports whether the scheme is https, which is
# the firmware's cue to stop verifying the certificate.
def base_url():
    url = OBS_URL.rstrip("/")
    return url, url.startswith("https://")


# TLS CONTEXT BUILDER
# Returns an unverified SSL context for https targets, matching the firmware
# skipping certificate checks so the plugin's self-signed cert is accepted.
def tls_context(insecure):
    if not insecure:
        return None
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    return context


# DAILY LINE RENDERER
# Fills the template, flattening the note onto one line the way save_daily
# does, and appends the newline that separates it from the next entry.
def daily_line(text):
    single = text.replace("\r", "").replace("\n", " ")
    line = OBS_LINE if OBS_LINE else "- {text}"
    line = line.replace("{time}", time.strftime("%H:%M"))
    line = line.replace("{text}", single)
    return line + "\n"


# NEW NOTE BUILDER
# Returns the (vault_path, body) pair save_note creates, with the same
# timestamped filename and frontmatter.
def new_note(text):
    name = time.strftime("%Y-%m-%d %H%M Voice note")
    folder = OBS_FOLDER.rstrip("/")
    path = (folder + "/" if folder else "") + name + ".md"
    body = ("---\ncreated: " + time.strftime("%Y-%m-%dT%H:%M:%S") +
            "\nsource: reterminal-sticky\n---\n\n" + text + "\n")
    return path, body


# VAULT REQUEST SENDER
# Sends one request with the plugin's bearer token and markdown content type,
# returning (status, body_text).
def send(method, url, body, insecure):
    request = urllib.request.Request(url, data=body.encode("utf-8"),
                                     method=method)
    request.add_header("Authorization", f"Bearer {OBS_KEY}")
    request.add_header("Content-Type", "text/markdown")
    try:
        with urllib.request.urlopen(request, timeout=20,
                                    context=tls_context(insecure)) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8", "replace")


# TEST RUNNER
# Validates the environment, saves the note through the configured mode, and
# prints the status and body.
def main(argv):
    if len(argv) != 2:
        print('usage: obsidian_test.py "note text"', file=sys.stderr)
        return 2
    if OBS_MODE not in ("daily", "note"):
        print("OBS_MODE must be daily or note", file=sys.stderr)
        return 2
    if not OBS_KEY:
        print("OBS_KEY is not set", file=sys.stderr)
        return 2

    url, insecure = base_url()
    if OBS_MODE == "daily":
        method, target, body = "POST", url + "/periodic/daily/", \
            daily_line(argv[1])
    else:
        path, body = new_note(argv[1])
        method, target = "PUT", url + "/vault/" + encode_path(path)
        print(f"  vault path: {path}")

    print(f"{method} {target}")
    print(f"  body: {body!r}")
    try:
        status, response_body = send(method, target, body, insecure)
    except (urllib.error.URLError, OSError) as error:
        print(f"request failed: {error}", file=sys.stderr)
        return 1
    print(f"HTTP {status}")
    print(response_body.strip())

    if status == 401:
        print("Key rejected. Copy it again from the Local REST API plugin "
              "settings.", file=sys.stderr)
    elif status == 404 and OBS_MODE == "daily":
        print("Periodic Notes plugin missing. Install the "
              '"Local REST API - Periodic Notes" companion plugin.',
              file=sys.stderr)
    return 0 if 200 <= status < 300 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
