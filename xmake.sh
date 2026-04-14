#!/usr/bin/env bash

set -eu

if ! command -v curl > /dev/null; then
    echo >&2 "please install curl"
    return 1
fi

function http_get
(
    HTTP_GET_URL="${1}"
    HTTP_GET_FILEPATH="${2:-"$(basename "${HTTP_GET_URL}")"}"

    if [ -z "${HTTP_GET_URL}" ]; then
        echo >&2 "usage: http_get <url> [filepath]"
        return 1
    fi

    if ! curl -fSL "${HTTP_GET_URL}" -o "${HTTP_GET_FILEPATH}.part"; then
        echo >&2 "failed to download ${HTTP_GET_URL} to ${HTTP_GET_FILEPATH}.part"
        return 1
    fi

    mv "${HTTP_GET_FILEPATH}.part" "${HTTP_GET_FILEPATH}"
)

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

# XMAKE_RELEASE="$(curl --silent "https://api.github.com/repos/xmake-io/xmake/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')"
XMAKE_RELEASE="v3.0.1"

XMAKE_URL="https://github.com/xmake-io/xmake/releases/download/${XMAKE_RELEASE}/xmake-bundle-${XMAKE_RELEASE}."

XMAKE_DIR="$SCRIPT_DIR/.xmake-local"
XMAKE_FILE="$XMAKE_DIR/xmake-bundle"
XMAKE_CMD="$XMAKE_FILE"
XMAKE_SHOULD_DOWNLOAD=true

ARCH="$(uname -m)"
if [ "$ARCH" = "x86_64" ]; then
    XMAKE_URL="${XMAKE_URL}linux.x86_64"
else
    XMAKE_URL="${XMAKE_URL}cosmocc"
    XMAKE_CMD="sh $XMAKE_CMD"
fi

if command -v xmake > /dev/null; then
    if xmake --version | grep -q $XMAKE_RELEASE; then
        XMAKE_CMD=xmake
        XMAKE_SHOULD_DOWNLOAD=false
    fi
fi

if [ "$XMAKE_SHOULD_DOWNLOAD" = true ]; then
    if  [ ! -f "$XMAKE_FILE" ]; then
        echo xmake $XMAKE_RELEASE not found. downloading...

        mkdir -p "$XMAKE_DIR"
        http_get "$XMAKE_URL" "$XMAKE_FILE"
    fi
    if [[ ! -x "$XMAKE_FILE" ]]; then
        chmod +x "$XMAKE_FILE"
    fi
fi

$XMAKE_CMD "$@"
