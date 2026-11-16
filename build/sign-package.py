#!/usr/bin/env python3
"""sign-package.py — sign an update package or image bundle in minisign armor.

`minicontainer update apply --verify=KEY` and `pull --sig=KEY` read a minisign
public key (42 decoded bytes: 2-byte algorithm, 8-byte key id, 32-byte Ed25519
public key) and a minisign signature file (74 decoded bytes: algorithm, key id,
64-byte signature). The runtime verifies the signature with
`openssl pkeyutl -verify -rawin`, i.e. **pure Ed25519 over the file**, which is
minisign's legacy "Ed" mode.

Minisign's default for large files is the prehashed "ED" mode (Ed25519ph with
BLAKE2b-512); that mode is NOT accepted by this runtime yet — see COMPAT.md.
Files signed here use "Ed" and therefore verify.

    ./sign-package.py keygen  --key priv.key --pub mc.pub
    ./sign-package.py sign    --key priv.key --pub mc.pub -m pkg.tar.gz

Requires python3 + the `cryptography` package (pip install cryptography).
"""
import argparse
import base64
import hashlib
import os
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)
from cryptography.hazmat.primitives.serialization import (
    Encoding,
    NoEncryption,
    PrivateFormat,
    PublicFormat,
)


def b64(data: bytes) -> str:
    return base64.b64encode(data).decode()


def key_id(pub: bytes) -> bytes:
    """minisign derives the key id from the public key; any stable 8 bytes work
    here because the runtime only compares key file vs signature file."""
    return hashlib.blake2b(pub, digest_size=8).digest()


def cmd_keygen(args) -> int:
    priv = Ed25519PrivateKey.generate()
    seed = priv.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
    pub = priv.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    with open(args.key, "wb") as f:
        f.write(seed)
    os.chmod(args.key, 0o600)
    with open(args.pub, "w") as f:
        f.write("untrusted comment: minicontainer update public key\n")
        f.write(b64(b"Ed" + key_id(pub) + pub) + "\n")
    print(f"key:  {args.key}\npub:  {args.pub}\nkeyid: {key_id(pub).hex()}")
    return 0


def cmd_sign(args) -> int:
    with open(args.key, "rb") as f:
        seed = f.read()
    priv = Ed25519PrivateKey.from_private_bytes(seed)
    pub = priv.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    kid = key_id(pub)
    with open(args.message, "rb") as f:
        data = f.read()
    sig = priv.sign(data)
    out = args.message + ".sig"
    with open(out, "w") as f:
        f.write("untrusted comment: signature from minicontainer dev key\n")
        f.write(b64(b"Ed" + kid + sig) + "\n")
        f.write("trusted comment: sign-package.py\n")
        f.write(b64(hashlib.blake2b(data, digest_size=64).digest()) + "\n")
    print(f"signed: {args.message} -> {out}")
    print(f"sha256: {hashlib.sha256(data).hexdigest()}")
    if args.verify_with:
        pubfile = args.verify_with
        with open(pubfile) as f:
            blob = base64.b64decode(f.read().strip().splitlines()[-1])
        Ed25519PublicKey.from_public_bytes(blob[10:]).verify(sig, data)
        print(f"self-check OK against {pubfile}")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("keygen")
    g.add_argument("--key", default="priv.key")
    g.add_argument("--pub", default="minicontainer.pub")
    g.set_defaults(func=cmd_keygen)
    s = sub.add_parser("sign")
    s.add_argument("--key", required=True)
    s.add_argument("--pub")
    s.add_argument("--verify-with", dest="verify_with")
    s.add_argument("-m", "--message", required=True)
    s.set_defaults(func=cmd_sign)
    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
