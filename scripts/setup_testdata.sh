#!/bin/sh
# Download Red Alert ISOs and extract testdata assets
#
# Red Alert was released as freeware by EA in 2008.
# ISOs sourced from Internet Archive.
#
# Usage: ./setup_testdata.sh [--keep-iso]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TESTDATA="$PROJECT_DIR/testdata"
DOWNLOAD_DIR="$PROJECT_DIR/downloads"

BASE_URL="https://archive.org/download/command-conquer-red-alert_202309"
CD1_ISO="Command%20%26%20Conquer%20Red%20Alert%20%5BCD1%5D.ISO"
CD2_ISO="Command%20%26%20Conquer%20Red%20Alert%20%5BCD2%5D.ISO"
CD1_FILE="CD1_ALLIED.ISO"
CD2_FILE="CD2_SOVIET.ISO"

KEEP_ISO=0
if [ "$1" = "--keep-iso" ]; then
    KEEP_ISO=1
fi

log() {
    echo "==> $1"
}

err() {
    echo "Error: $1" >&2
    exit 1
}

download_iso() {
    url="$1"
    dest="$2"
    if [ -f "$dest" ]; then
        log "Already exists: $dest"
        return 0
    fi
    log "Downloading: $dest"
    curl -L -o "$dest" "$url" || err "Download failed: $url"
}

mount_iso() {
    iso="$1"
    log "Mounting: $iso"
    hdiutil attach -nobrowse -readonly "$iso" 2>/dev/null \
        || err "Mount failed: $iso"
}

unmount_vol() {
    vol="$1"
    if [ -d "$vol" ]; then
        log "Unmounting: $vol"
        hdiutil detach "$vol" 2>/dev/null || true
    fi
}

extract_mix() {
    src="$1"
    prefix="$2"
    dest="$TESTDATA/mix"
    mkdir -p "$dest"

    # MAIN.MIX at root
    if [ -f "$src/MAIN.MIX" ]; then
        log "  ${prefix}_main.mix"
        cp "$src/MAIN.MIX" "$dest/${prefix}_main.mix"
    fi

    # Nested MIX files
    for f in "$src/INSTALL/REDALERT.MIX" \
             "$src/SETUP/AUD.MIX" \
             "$src/SETUP/SETUP.MIX"; do
        if [ -f "$f" ]; then
            name=$(basename "$f" | tr '[:upper:]' '[:lower:]')
            subdir=$(dirname "$f")
            subdir=$(basename "$subdir" | tr '[:upper:]' '[:lower:]')
            out="${prefix}_${subdir}_${name}"
            log "  $out"
            cp "$f" "$dest/$out"
        fi
    done
}

extract_vqa() {
    src="$1"
    prefix="$2"
    dest="$TESTDATA/vqa"
    mkdir -p "$dest"

    for f in "$src"/*.VQA "$src"/*.VQP; do
        if [ -f "$f" ]; then
            name=$(basename "$f")
            log "  ${prefix}_${name}"
            cp "$f" "$dest/${prefix}_${name}"
        fi
    done
}

extract_ard() {
    src="$1"
    prefix="$2"
    dest="$TESTDATA/ard"
    mkdir -p "$dest"

    if [ -d "$src/ard" ]; then
        for f in "$src/ard"/*; do
            if [ -f "$f" ]; then
                name=$(basename "$f")
                cp "$f" "$dest/${prefix}_ard_${name}"
            fi
        done
        count=$(ls -1 "$src/ard" | wc -l | tr -d ' ')
        log "  $count files from ard/"
    fi
}

extract_ico() {
    src="$1"
    prefix="$2"
    dest="$TESTDATA/ico"
    mkdir -p "$dest"

    for f in "$src/INSTALL"/*.ICO "$src/INTERNET"/*.ICO; do
        if [ -f "$f" ]; then
            dir=$(dirname "$f" | sed "s|$src/||" | tr '[:upper:]' '[:lower:]')
            name=$(basename "$f")
            log "  ${prefix}_${dir}_${name}"
            cp "$f" "$dest/${prefix}_${dir}_${name}"
        fi
    done
}

extract_cd() {
    vol="$1"
    prefix="$2"

    log "Extracting $prefix from $vol"
    extract_mix "$vol" "$prefix"
    extract_vqa "$vol" "$prefix"
    extract_ard "$vol" "$prefix"
    extract_ico "$vol" "$prefix"
}

cleanup() {
    unmount_vol "/Volumes/CD1"
    unmount_vol "/Volumes/CD2"
    if [ "$KEEP_ISO" -eq 0 ] && [ -d "$DOWNLOAD_DIR" ]; then
        log "Removing ISOs"
        rm -f "$DOWNLOAD_DIR/$CD1_FILE" "$DOWNLOAD_DIR/$CD2_FILE"
        rmdir "$DOWNLOAD_DIR" 2>/dev/null || true
    fi
}

main() {
    trap cleanup EXIT

    log "Setting up testdata for mix-tool"
    log "Source: Internet Archive (Red Alert freeware release)"

    mkdir -p "$DOWNLOAD_DIR"
    mkdir -p "$TESTDATA"

    # Download ISOs
    download_iso "$BASE_URL/$CD1_ISO" "$DOWNLOAD_DIR/$CD1_FILE"
    download_iso "$BASE_URL/$CD2_ISO" "$DOWNLOAD_DIR/$CD2_FILE"

    # Mount and extract CD1
    unmount_vol "/Volumes/CD1"
    mount_iso "$DOWNLOAD_DIR/$CD1_FILE"
    extract_cd "/Volumes/CD1" "cd1"
    unmount_vol "/Volumes/CD1"

    # Mount and extract CD2
    unmount_vol "/Volumes/CD2"
    mount_iso "$DOWNLOAD_DIR/$CD2_FILE"
    extract_cd "/Volumes/CD2" "cd2"
    unmount_vol "/Volumes/CD2"

    log "Done. Testdata extracted to: $TESTDATA"
    du -sh "$TESTDATA"/*
}

main "$@"
