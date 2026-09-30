#!/usr/bin/env bash
# Compara el estado final de cada versión contra la secuencial.
# Uso: ./scripts/check_correctness.sh [N] [steps] [threads]
set -euo pipefail

N=${1:-5000}
STEPS=${2:-10}
THREADS=${3:-8}
DIR=results/states

cd "$(dirname "$0")/.."
make -s
mkdir -p "$DIR"

echo "== Secuencial (referencia) =="
./bin/nbody_secuencial "$N" "$STEPS" --out "$DIR/seq.txt" | grep -E "Mode|Checksum"

if [[ ! -x bin/nbody_paralelo ]]; then
    echo "bin/nbody_paralelo no existe todavía: solo se generó la referencia."
    exit 0
fi

for MODE in parallel optimized; do
    echo
    echo "== $MODE ($THREADS threads) =="
    ./bin/nbody_paralelo "$N" "$STEPS" --mode "$MODE" --threads "$THREADS" \
        --out "$DIR/$MODE.txt" | grep -E "Mode|Checksum"
    ./bin/compare_states "$DIR/seq.txt" "$DIR/$MODE.txt" || true
done
