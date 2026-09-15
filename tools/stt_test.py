#!/usr/bin/env python3
# =============================================================================
# SPEECH-TO-TEXT REQUEST TESTER
# =============================================================================
# Sends a WAV file to the transcription endpoint with byte-for-byte the same
# multipart body main/net/http.cpp post_wav builds, for debugging from a
# desktop instead of from the board.
#
# Usage: stt_test.py <file.wav>
# Env:   STT_URL, STT_MODEL, STT_KEY, STT_LANG (defaults match
#        main/app/settings.cpp)

import os
import sys
import urllib.error
import urllib.request

STT_URL = os.environ.get(
    "STT_URL", "https://api.groq.com/openai/v1/audio/transcriptions")
STT_MODEL = os.environ.get("STT_MODEL", "whisper-large-v3-turbo")
STT_KEY = os.environ.get("STT_KEY", "")
STT_LANG = os.environ.get("STT_LANG", "")

# Same literal boundary as kBoundary in main/net/http.cpp, so a capture from
# the board and a capture from this script diff cleanly.
BOUNDARY = "----ObsidianStickyBoundary7f3a9c"


# MULTIPART BODY BUILDER
# Assembles the text fields followed by the WAV part, in the order post_wav
# writes them.
def build_body(fields, wav_bytes):
    out = bytearray()
    for name, value in fields:
        out += f"--{BOUNDARY}\r\n".encode()
        out += f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode()
        out += value.encode("utf-8") + b"\r\n"
    out += f"--{BOUNDARY}\r\n".encode()
    out += (b'Content-Disposition: form-data; name="file"; '
            b'filename="note.wav"\r\n')
    out += b"Content-Type: audio/wav\r\n\r\n"
    out += wav_bytes
    out += f"\r\n--{BOUNDARY}--\r\n".encode()
    return bytes(out)


# FIELD COLLECTOR
# Returns the form fields stt_client.cpp sends, omitting language when unset.
def build_fields():
    fields = [("model", STT_MODEL), ("response_format", "text")]
    if STT_LANG:
        fields.append(("language", STT_LANG))
    return fields


# TRANSCRIPTION POSTER
# Uploads the WAV and returns (status, body_text), raising nothing on an HTTP
# error status.
def post_wav(wav_bytes):
    request = urllib.request.Request(STT_URL, data=build_body(build_fields(),
                                                              wav_bytes),
                                     method="POST")
    request.add_header("Authorization", f"Bearer {STT_KEY}")
    request.add_header("Content-Type",
                       f"multipart/form-data; boundary={BOUNDARY}")
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8", "replace")


# TEST RUNNER
# Validates the environment and the argument, posts the clip, and prints the
# status and body.
def main(argv):
    if len(argv) != 2:
        print("usage: stt_test.py <file.wav>", file=sys.stderr)
        return 2
    if not STT_KEY:
        print("STT_KEY is not set", file=sys.stderr)
        return 2
    try:
        with open(argv[1], "rb") as handle:
            wav_bytes = handle.read()
    except OSError as error:
        print(f"cannot read {argv[1]}: {error}", file=sys.stderr)
        return 2

    print(f"POST {STT_URL}")
    print(f"  model={STT_MODEL} response_format=text "
          f"language={STT_LANG or '(auto)'} wav={len(wav_bytes)} bytes")
    try:
        status, body = post_wav(wav_bytes)
    except (urllib.error.URLError, OSError) as error:
        print(f"request failed: {error}", file=sys.stderr)
        return 1
    print(f"HTTP {status}")
    print(body.strip())
    return 0 if 200 <= status < 300 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
