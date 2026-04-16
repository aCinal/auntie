#!/usr/bin/python

import subprocess
from random import randbytes
import os

def get_mrenclave(enclave_binary):
    """Get MRENCLAVE measurement of a signed SGX enclave binary."""
    dumpfile = "/tmp/auntie_enclave_dump" + randbytes(16).hex() + ".txt"

    # Set the path to sgx_sign
    env = os.environ.copy()
    env["PATH"] = "/opt/intel/sgxsdk/bin/x64"

    # Dump metadata from the enclave image
    result = subprocess.run([
        "sgx_sign", "dump",
        "-enclave", enclave_binary,
        "-dumpfile", dumpfile
    ], capture_output=True, text=True, env=env)
    if result.returncode:
        raise Exception(f"sgx_sign failed: {result.stderr.splitlines()[0]}")

    with open(dumpfile, "r") as f:
        lines = f.readlines()

    # Find MRENCLAVE in the dump file
    header = lines.index("metadata->enclave_css.body.enclave_hash.m:\n")
    # Drop newlines and join the lines together
    mrenclave_hex = lines[header + 1].strip() + " " + lines[header + 2].strip()
    mrenclave = bytes([ int(byte, 16) for byte in mrenclave_hex.split() ])

    # Clean up
    os.unlink(dumpfile)

    return mrenclave

if __name__ == "__main__":
    print(f"MRENCLAVE (Operator's TEE): {get_mrenclave('../auntie_operator_tee.signed.so').hex()}")
    print(f"MRENCLAVE (Player's TEE):   {get_mrenclave('../auntie_player_tee.signed.so').hex()}")
