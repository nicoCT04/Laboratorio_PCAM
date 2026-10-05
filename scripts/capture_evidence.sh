#!/usr/bin/env bash
set -euo pipefail

N=${1:-20000}
STEPS=${2:-10}
THREADS=${3:-8}

cd "$(dirname "$0")/.."
OUT=evidence
PROMPT="$(whoami)@$(hostname 2>/dev/null || cat /etc/hostname):~/Laboratorio_PCAM\$ "
mkdir -p "$OUT"

capture() {
    local name=$1 title=$2
    shift 2
    local txt="$OUT/$name.txt"
    : >"$txt"
    for cmd in "$@"; do
        echo "\$ $cmd" >>"$txt"
        bash -c "$cmd" >>"$txt" 2>&1
    done
    python3 scripts/render_evidence.py "$txt" "$OUT/$name.png" "$title" "$PROMPT"
    echo "$OUT/$name.png"
}

capture compile "Evidencia 1 - Compilación" \
    "make clean && make" \
    "ls -l bin/"

capture sequential "Evidencia 2 - Ejecución secuencial (A)" \
    "./bin/nbody_secuencial $N $STEPS"

capture parallel "Evidencia 3 - Ejecución paralela directa (B)" \
    "./bin/nbody_paralelo $N $STEPS --mode parallel --threads $THREADS --schedule dynamic --chunk 64"

capture optimized "Evidencia 4 - Ejecución optimizada por pares (C)" \
    "./bin/nbody_paralelo $N $STEPS --mode optimized --threads $THREADS --schedule dynamic --chunk 64"

capture hardware "Evidencia 5 - Resumen de hardware" \
    "LC_ALL=C lscpu | grep -E '^(Model name|CPU\\(s\\)|Thread\\(s\\) per core|Core\\(s\\) per socket):'" \
    "./bin/nbody_paralelo $N 1 --mode parallel --threads $THREADS | grep -E 'Threads|Processors|Max threads'"

capture correctness "Extra - Corrección contra la secuencial" \
    "./scripts/check_correctness.sh $N $STEPS $THREADS"
