#!/usr/bin/env bash
# Sync / build external dependencies for OnionHEN.
#
# Embedded payload input: kstuff.elf
# Also built from source: onion_elfldr.elf (private 9020 runtime loader)
# Removed: external elfldr.elf (9021 service), ps5debug, ps5-app-dumper, Byepervisor/hen.bin
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="${ROOT}/third_party"
CACHE="${ONIONHEN_CACHE_DIR:-${ROOT}/.cache/dependencies}"

PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
FROM_SOURCE=0
STUB_MISSING=0
INIT_SUBMODULES=0
FORCE_DOWNLOAD=0

KSTUFF_URL="https://github.com/EchoStretch/kstuff-lite/releases/download/v1.11/kstuff.elf"
KSTUFF_SOURCE_DIR="${TP}/kstuff-lite"
# Real release blob is hundreds of KB+; stubs are tiny markers.
KSTUFF_MIN_BYTES=65536
# Official GitHub release v0.0.1 is 12.70-only. The bundled ELF is rebuilt from
# onionHEN-ftpsrv-plugin 8823e89 (ftpsrv 1.16-ng-stable, SELF through 13.60).
FTPSRV_URL=""
FTPSRV_SHA256="400b4d0132584d1e20786d9dc50feda1d64cffd3ee4cbf6f2787b2788bb20888"
FTPSRV_MIN_BYTES=65536

log()  { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ok]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<EOF
Sync OnionHEN external dependencies from open-source upstreams.

Submodules (under third_party/):
  kstuff-lite      https://github.com/EchoStretch/kstuff-lite
Runtime-only external dependency:
  elfldr @ 9021    https://github.com/ps5-payload-dev/elfldr
                  Required only for initial bootstrap; OnionHEN starts
                  onion_elfldr.elf @ 9020 for runtime launches.

Removed from OnionHEN (not synced):
  external elfldr.elf (9021 service), ps5debug, ps5-app-dumper, Byepervisor/hen.bin

Options:
  --init-submodules   git submodule update --init --recursive
  --from-source       Require source checkouts; disable release fallbacks
  --stub-missing      Write tiny placeholders if download/build fails
  --force             Re-download/rebuild external dependencies
  -h, --help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --from-source) FROM_SOURCE=1; shift ;;
    --stub-missing) STUB_MISSING=1; shift ;;
    --init-submodules) INIT_SUBMODULES=1; shift ;;
    --force) FORCE_DOWNLOAD=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

download() {
  local url="$1" dest="$2"
  mkdir -p "$(dirname "${dest}")"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 -o "${dest}.tmp" "${url}"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "${dest}.tmp" "${url}"
  else
    return 1
  fi
  mv -f "${dest}.tmp" "${dest}"
}

place() {
  local src="$1" dest="$2"
  mkdir -p "$(dirname "${dest}")"
  cp -f "${src}" "${dest}"
  ok "$(basename "${dest}") <- ${src}"
}

file_sha256() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${path}" | awk '{print $1}'
  else
    shasum -a 256 "${path}" | awk '{print $1}'
  fi
}

download_verified() {
  local url="$1" dest="$2" expected="$3"
  if ! download "${url}" "${dest}"; then
    return 1
  fi
  if [[ "$(file_sha256 "${dest}")" != "${expected}" ]]; then
    warn "checksum mismatch for $(basename "${dest}")"
    rm -f "${dest}"
    return 1
  fi
  return 0
}

stub() {
  local dest="$1" name="$2"
  mkdir -p "$(dirname "${dest}")"
  printf 'OnionHEN-STUB:%s\0' "${name}" > "${dest}"
  warn "STUB ${dest} (not for real hardware)"
}

need_sdk() {
  [[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] || \
    die "PS5_PAYLOAD_SDK required to build from source"
  export PS5_PAYLOAD_SDK
  export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"
}

init_submodules() {
  log "git submodule update --init --recursive"
  git -C "${ROOT}" submodule update --init --recursive
  ok "submodules ready"
}

kstuff_looks_cached() {
  local path="$1"
  [[ -f "${path}" ]] || return 1
  local sz
  sz="$(wc -c < "${path}" | tr -d ' ')"
  # Reject empty / OnionHEN-STUB placeholders from --stub-missing.
  if [[ "${sz}" -lt "${KSTUFF_MIN_BYTES}" ]]; then
    return 1
  fi
  if head -c 16 "${path}" 2>/dev/null | grep -q 'OnionHEN-STUB'; then
    return 1
  fi
  return 0
}

sync_kstuff() {
  local dest="${CACHE}/kstuff.elf"

  # Default path: reuse local blob so every build.sh does not re-hit GitHub.
  if [[ "${FORCE_DOWNLOAD}" -eq 0 && "${FROM_SOURCE}" -eq 0 ]] &&
      kstuff_looks_cached "${dest}"; then
    ok "kstuff.elf already present ($(wc -c < "${dest}" | tr -d ' ') bytes) — skip download"
    return 0
  fi

  if [[ "${FROM_SOURCE}" -eq 0 ]]; then
    log "kstuff: download kstuff-lite release"
    if download "${KSTUFF_URL}" "${dest}"; then
      ok "kstuff.elf (kstuff-lite v1.11)"
      return 0
    fi
    warn "download failed, trying submodule build"
  fi

  if [[ -d "${KSTUFF_SOURCE_DIR}" ]]; then
    need_sdk
    log "kstuff: build third_party/kstuff-lite (best-effort)"
    if [[ -x "${KSTUFF_SOURCE_DIR}/ci-ps5-kstuff-ldr.sh" ]]; then
      (cd "${KSTUFF_SOURCE_DIR}" && bash ./ci-ps5-kstuff-ldr.sh) || true
    elif [[ -f "${KSTUFF_SOURCE_DIR}/Makefile" ]]; then
      make -C "${KSTUFF_SOURCE_DIR}" -j4 || true
    fi
    local found
    found="$(find "${KSTUFF_SOURCE_DIR}" -name 'kstuff.elf' 2>/dev/null | head -1 || true)"
    if [[ -n "${found}" ]]; then
      place "${found}" "${dest}"
      return 0
    fi
  fi

  if [[ "${STUB_MISSING}" -eq 1 ]]; then
    stub "${dest}" "kstuff.elf"
    return 0
  fi
  die "kstuff.elf unavailable (prefer release download)"
}

ftpsrv_looks_cached() {
  local path="$1"
  [[ -f "${path}" ]] || return 1
  local sz
  sz="$(wc -c < "${path}" | tr -d ' ')"
  if [[ "${sz}" -lt "${FTPSRV_MIN_BYTES}" ]]; then
    return 1
  fi
  if head -c 16 "${path}" 2>/dev/null | grep -q 'OnionHEN-STUB'; then
    return 1
  fi
  return 0
}

sync_ftpsrv() {
  local dest="${CACHE}/FTPS00001.elf"

  if ftpsrv_looks_cached "${dest}"; then
    local got
    got="$(file_sha256 "${dest}")"
    if [[ "${got}" == "${FTPSRV_SHA256}" ]]; then
      ok "FTPS00001.elf 13.60 rebuild ($(wc -c < "${dest}" | tr -d ' ') bytes)"
      return 0
    fi
    warn "FTPS00001.elf checksum mismatch (got ${got})"
  fi

  if [[ -n "${FTPSRV_URL}" ]] && download_verified "${FTPSRV_URL}" "${dest}" "${FTPSRV_SHA256}"; then
    ok "FTPS00001.elf (onionHEN-ftpsrv-plugin 13.60 rebuild)"
    return 0
  fi

  if [[ "${STUB_MISSING}" -eq 1 ]]; then
    stub "${dest}" "FTPS00001.elf"
    return 0
  fi
  die "FTPS00001.elf unavailable (rebuild onionHEN-ftpsrv-plugin main / 1.16-ng-stable; official GitHub v0.0.1 is 12.70-only)"
}

main() {
  log "OnionHEN dependency sync"
  echo "  third_party = ${TP}"
  echo "  cache       = ${CACHE}"

  mkdir -p "${CACHE}"

  if [[ "${INIT_SUBMODULES}" -eq 1 ]]; then
    init_submodules
  elif [[ ! -d "${TP}/kstuff-lite" ]]; then
    warn "submodules not checked out — run: git submodule update --init --recursive"
    warn "kstuff release downloads still work without submodules"
  fi

  sync_kstuff
  sync_ftpsrv

  log "Dependency sync done"
  ls -lah "${CACHE}/kstuff.elf" "${CACHE}/FTPS00001.elf" 2>/dev/null || true
}

main
