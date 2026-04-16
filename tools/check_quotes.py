#!/usr/bin/python

import subprocess
import sys
import os
from mrenclave import get_mrenclave

def verify_attestations(remote_parties):
    """Verify attestations of remote parties."""

    verifier = subprocess.Popen(
        ["./sgx_quote_checker/sgx_quote_checker"],
        stdout=subprocess.PIPE,
        stdin=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0
    )

    for i, (mrenclave, report_data, quote) in enumerate(remote_parties):
        verifier.stdin.write(mrenclave)
        verifier.stdin.write(report_data)
        verifier.stdin.write(len(quote).to_bytes(4, "little"))
        verifier.stdin.write(quote)
        verifier.stdin.flush()
        status = verifier.stdout.readline()
        if status != b"Quote ok\n":
            raise Exception(f"Quote verification failed for party {i}: {verifier.stderr.read()}")

    print("All quotes ok")

def check_quotes_as_operator(filename):
    """Check players' quotes as the operator."""
    # Get MRENCLAVE measurement of player TEE
    player_mrenclave = get_mrenclave("../auntie_player_tee.signed.so")
    # Parse the quotes file
    remote_parties = [ (player_mrenclave, report_data, quote) for (report_data, quote) in parse_quotes_file(filename) ]
    verify_attestations(remote_parties)

def check_quotes_as_player(filename):
    """Check counterparties' quotes as a player."""
    # Get MRENCLAVE measurements of both types of TEEs
    player_mrenclave = get_mrenclave("../auntie_player_tee.signed.so")
    operator_mrenclave = get_mrenclave("../auntie_operator_tee.signed.so")
    # Parse the quotes file
    report_data_and_quotes = parse_quotes_file(filename)
    remote_parties = [ (player_mrenclave, report_data, quote) for (report_data, quote) in report_data_and_quotes[:-1] ]
    # The last quote belongs to the operator's TEE, use appropriate MRENCLVAE
    operator_report_data, operator_quote = report_data_and_quotes[-1]
    remote_parties.append((operator_mrenclave, operator_report_data, operator_quote))
    verify_attestations(remote_parties)

def parse_quotes_file(filename):
    """Parse a quotes file."""
    with open(filename) as f:
        quote_lines = f.readlines()
    split_lines = [ line.split(", ") for line in quote_lines ]
    return [ (bytes.fromhex(line[0]), bytes.fromhex(line[1])) for line in split_lines ]

if __name__ == "__main__":
    party = sys.argv[1]
    filename = sys.argv[2]
    if party == "o":
        print("Verifying quotes as the operator")
        check_quotes_as_operator(filename)
    elif party == "p":
        print("Verifying quotes as a player")
        check_quotes_as_player(filename)
    else:
        raise Exception(f"Unknown option {party}")
