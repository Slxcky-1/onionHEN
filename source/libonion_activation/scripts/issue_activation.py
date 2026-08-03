#!/usr/bin/env python3
"""
Issue OnionHEN beta Ed25519 license certificates.

Requires: pip install cryptography   (or pynacl)

Example:
  ./issue_activation.py \\
      --device-id 'sha256:abcd...' \\
      --seed-hex 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 \\
      --out license.json

The public key printed by this tool must be supplied at payload build time:

  export ONION_ACTIVATION_PUBLIC_KEY_HEX=<printed public_key_hex>
"""

from __future__ import annotations

import argparse
import json
import secrets
import sys
import time


def ed25519_sign(seed: bytes, message: bytes) -> tuple[bytes, bytes]:
    """Return (public_key, signature). Prefer cryptography, then nacl."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )

        private = Ed25519PrivateKey.from_private_bytes(seed)
        public = private.public_key().public_bytes_raw()
        signature = private.sign(message)
        return public, signature
    except ImportError:
        pass
    try:
        from nacl.signing import SigningKey

        sk = SigningKey(seed)
        signed = sk.sign(message)
        return bytes(sk.verify_key), signed.signature
    except ImportError:
        pass
    raise SystemExit(
        "needs 'cryptography' or 'pynacl' (pip install cryptography)"
    )


def build_payload(
    version: int,
    license_id: str,
    subject: str,
    device_id: str,
    features: list[str],
    issued_at: int,
    expires_at: int,
    nonce: str,
) -> str:
    feat = ",".join(features)
    return (
        f"version={version}\n"
        f"licenseId={license_id}\n"
        f"subject={subject}\n"
        f"deviceId={device_id}\n"
        f"features={feat}\n"
        f"issuedAt={issued_at}\n"
        f"expiresAt={expires_at}\n"
        f"nonce={nonce}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Issue OnionHEN beta Ed25519 license.json"
    )
    parser.add_argument(
        "--device-id",
        required=True,
        help="full device id, e.g. sha256:<64 hex>",
    )
    parser.add_argument(
        "--seed-hex",
        required=True,
        help="32-byte Ed25519 seed as 64 hex chars",
    )
    parser.add_argument("--license-id")
    parser.add_argument("--subject", default="beta")
    parser.add_argument("--features", default="*")
    parser.add_argument("--issued-at", type=int)
    parser.add_argument("--expires-at", type=int, default=0)
    parser.add_argument("--nonce")
    parser.add_argument("--out", help="write license.json to path")
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    device_id = args.device_id.strip()
    if not device_id.startswith("sha256:") or len(device_id) < 7 + 32:
        raise SystemExit("--device-id must look like sha256:<hex>")

    if len(args.seed_hex) != 64:
        raise SystemExit("--seed-hex must be 64 hex chars (32-byte seed)")
    seed = bytes.fromhex(args.seed_hex)

    issued_at = args.issued_at if args.issued_at is not None else int(time.time())
    expires_at = args.expires_at if args.expires_at is not None else 0
    license_id = args.license_id or f"OHN-{int(time.time())}"
    subject = args.subject or "beta"
    features = [f.strip() for f in (args.features or "*").split(",") if f.strip()]
    nonce = args.nonce or secrets.token_hex(8)

    payload = build_payload(
        1,
        license_id,
        subject,
        device_id,
        features,
        issued_at,
        expires_at,
        nonce,
    )
    public, signature = ed25519_sign(seed, payload.encode("utf-8"))
    body = {
        "version": 1,
        "licenseId": license_id,
        "subject": subject,
        "deviceId": device_id,
        "features": features,
        "issuedAt": issued_at,
        "expiresAt": expires_at,
        "nonce": nonce,
        "signature": signature.hex(),
    }
    text = json.dumps(
        body,
        indent=2 if args.pretty else None,
        separators=None if args.pretty else (",", ":"),
    )
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
            if not text.endswith("\n"):
                f.write("\n")
        print(f"wrote {args.out}", file=sys.stderr)
    else:
        print(text)
    print(f"public_key_hex={public.hex()}", file=sys.stderr)
    print(
        "Ensure the payload is built with "
        f"-DONION_ACTIVATION_PUBLIC_KEY_HEX={public.hex()}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
