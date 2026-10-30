#!/bin/sh
# Build every compiled solver in this library.
#
# -lm is required for the nonogram and SAT solvers (they call log()/exp()); the
# link fails with "undefined reference to `log'" without it.
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
FLAGS="-O3 -march=native"

echo "== nonogram (nonobp)"
$CC $FLAGS -o nonogram/nonobp nonogram/nonobp.c -lm

echo "== sat (wsatk, exactksat)"
$CC $FLAGS -o sat/wsatk sat/wsatk.c -lm
$CC $FLAGS -o sat/exactksat sat/exactksat.c -lm

echo "== pow (sha256 / keccak miners)"
$CC $FLAGS -pthread -o pow/sha256_prefix_nonce pow/sha256_prefix_nonce.c
$CC $FLAGS -pthread -o pow/keccak_prefix_nonce pow/keccak_prefix_nonce.c
$CC $FLAGS -pthread -o pow/keccak_challenge_nonce pow/keccak_challenge_nonce.c

echo "== done"
ls -1 nonogram/nonobp sat/wsatk sat/exactksat pow/sha256_prefix_nonce \
      pow/keccak_prefix_nonce pow/keccak_challenge_nonce
