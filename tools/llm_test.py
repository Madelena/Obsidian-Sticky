#!/usr/bin/env python3
# =============================================================================
# CLEANUP MODEL REQUEST TESTER
# =============================================================================
# Sends a transcript through the cleanup model with the same request bodies
# and headers main/net/llm_client.cpp builds for the anthropic and openai
# kinds, for debugging from a desktop instead of from the board.
#
# Usage: llm_test.py "raw transcript text"
# Env:   LLM_KIND, LLM_URL, LLM_MODEL, LLM_KEY, LLM_PROMPT (defaults match
#        main/app/settings.cpp)

import json
import os
import sys
import urllib.error
import urllib.request

DEFAULT_PROMPT = (
    "You turn a raw voice transcription into a clean written note. Fix "
    "punctuation, capitalization, and obvious mis-hearings. Remove filler words "
    "(um, uh, like, you know), stutters, and repeated words. When the speaker "
    "corrects themselves (\"I mean\", \"no wait\", \"actually\"), keep only the "
    "corrected version. Keep the speaker's own words, order, and meaning "
    "otherwise: do not summarize, add ideas, or drop content. Reply with only "
    "the cleaned text.")

LLM_KIND = os.environ.get("LLM_KIND", "anthropic")
LLM_URL = os.environ.get("LLM_URL", "https://api.anthropic.com/v1/messages")
LLM_MODEL = os.environ.get("LLM_MODEL", "claude-haiku-4-5")
LLM_KEY = os.environ.get("LLM_KEY", "")
LLM_PROMPT = os.environ.get("LLM_PROMPT", DEFAULT_PROMPT)


# ANTHROPIC BODY BUILDER
# Returns the Messages API request object, with the prompt as a top-level
# system string rather than a message.
def anthropic_body(user_text):
    return {
        "model": LLM_MODEL,
        "max_tokens": 1024,
        "system": LLM_PROMPT,
        "messages": [{"role": "user", "content": user_text}],
    }


# OPENAI BODY BUILDER
# Returns the chat-completions request object, with the prompt as the first
# message.
def openai_body(user_text):
    return {
        "model": LLM_MODEL,
        "messages": [
            {"role": "system", "content": LLM_PROMPT},
            {"role": "user", "content": user_text},
        ],
    }


# HEADER BUILDER
# Returns the auth headers for the selected provider.
def build_headers(anthropic):
    headers = {"Content-Type": "application/json"}
    if anthropic:
        headers["x-api-key"] = LLM_KEY
        headers["anthropic-version"] = "2023-06-01"
    else:
        headers["Authorization"] = f"Bearer {LLM_KEY}"
    return headers


# REPLY TEXT EXTRACTOR
# Digs the assistant text out of either provider's response shape, returning
# None when the reply does not match.
def extract_text(anthropic, body):
    try:
        parsed = json.loads(body)
        if anthropic:
            return parsed["content"][0]["text"]
        return parsed["choices"][0]["message"]["content"]
    except (ValueError, KeyError, IndexError, TypeError):
        return None


# CLEANUP POSTER
# Sends the request and returns (status, body_text).
def post_chat(anthropic, user_text):
    body = anthropic_body(user_text) if anthropic else openai_body(user_text)
    data = json.dumps(body, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(LLM_URL, data=data, method="POST")
    for name, value in build_headers(anthropic).items():
        request.add_header(name, value)
    try:
        with urllib.request.urlopen(request, timeout=40) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8", "replace")


# TEST RUNNER
# Validates the environment and the argument, sends the transcript, and prints
# the status, raw body, and the extracted reply.
def main(argv):
    if len(argv) != 2:
        print('usage: llm_test.py "raw transcript text"', file=sys.stderr)
        return 2
    if LLM_KIND not in ("anthropic", "openai"):
        print("LLM_KIND must be anthropic or openai", file=sys.stderr)
        return 2
    if not LLM_KEY:
        print("LLM_KEY is not set", file=sys.stderr)
        return 2

    anthropic = LLM_KIND == "anthropic"
    print(f"POST {LLM_URL}")
    print(f"  kind={LLM_KIND} model={LLM_MODEL}")
    try:
        status, body = post_chat(anthropic, argv[1])
    except (urllib.error.URLError, OSError) as error:
        print(f"request failed: {error}", file=sys.stderr)
        return 1
    print(f"HTTP {status}")
    print(body.strip())
    if not 200 <= status < 300:
        return 1

    text = extract_text(anthropic, body)
    if text is None:
        print("Unexpected reply shape", file=sys.stderr)
        return 1
    # The firmware trims the trailing newline and spaces most models append.
    print("\n--- cleaned ---")
    print(text.rstrip("\n "))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
