#!/usr/bin/env bash

set -eu

if ! command -v curl > /dev/null; then
    echo >&2 "please install curl"
    return 1
fi

function get_latest_release
{
    curl --silent "https://api.github.com/repos/xmake-io/xmake/releases/latest" |
        grep '"tag_name":' |
        sed -E 's/.*"([^"]+)".*/\1/'
}

function http_get
(
    http_get_url="${1}"
    http_get_filepath="${2:-"$(basename "${http_get_url}")"}"

    if [ -z "${http_get_url}" ]; then
        echo >&2 "usage: http_get <url> [filepath]"
        return 1
    fi

    if ! curl -fSL "${http_get_url}" -o "${http_get_filepath}.part"; then
        echo >&2 "failed to download ${http_get_url} to ${http_get_filepath}.part"
        return 1
    fi

    mv "${http_get_filepath}.part" "${http_get_filepath}"
)

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

XMAKE_RELEASE="$(get_latest_release)"
XMAKE_URL="https://github.com/xmake-io/xmake/releases/download/${XMAKE_RELEASE}/xmake-bundle-${XMAKE_RELEASE}.linux.x86_64"

XMAKE_DIR="$SCRIPT_DIR/.xmake-local"
XMAKE_EXEC="$XMAKE_DIR/xmake-bundle"

if command -v xmake > /dev/null; then
    XMAKE_EXEC=xmake
else
    if  [ ! -f "$XMAKE_EXEC" ]; then
        echo xmake not found. downloading...
        mkdir -p "$XMAKE_DIR"
        http_get "$XMAKE_URL" "$XMAKE_EXEC"
    fi
    if [[ ! -x "$XMAKE_EXEC" ]]; then
        sudo chmod +x "$XMAKE_EXEC"
    fi
fi

"$XMAKE_EXEC" "$@"
