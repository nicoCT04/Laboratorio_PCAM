#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Uso: $0 [N] [steps] [runs]"
    echo "  N=20000 steps=10 runs=3 por defecto"
    echo "  EXTRA_THREADS=\"16 32\" agrega puntos de escalabilidad (vacío para omitirlos)"
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

N=${1:-20000}
STEPS=${2:-10}
RUNS=${3:-3}
EXTRA_THREADS=${EXTRA_THREADS-16 32}

cd "$(dirname "$0")/.."
make -s

RAW_DIR=results/raw
CSV=results.csv
ENV_FILE=results/environment.txt
mkdir -p "$RAW_DIR"
rm -f "$RAW_DIR"/*.txt

field() {
    grep -E "^$1:" | head -1 | sed -E "s/^$1: ([^ ]+).*/\1/"
}

{
    echo "date: $(date -Iseconds)"
    echo "N: $N | Steps: $STEPS | Seed: 42 | Runs: $RUNS"
    echo "host: $(uname -srm)"
    if command -v lscpu >/dev/null; then
        LC_ALL=C lscpu | grep -E "^(Model name|CPU\(s\)|Thread\(s\) per core|Core\(s\) per socket|Socket\(s\)):" || true
    else
        sysctl -n machdep.cpu.brand_string 2>/dev/null || true
    fi
    echo "compiler: $(${CC:-cc} --version 2>/dev/null | head -1)"
    ./bin/nbody_secuencial 2 1 | grep -E "^(Processors|Max threads)"
} >"$ENV_FILE"

SEQ_AVG=""
echo "version,threads,schedule,chunk,run1,run2,run3,avg_time,speedup,efficiency,checksum" >"$CSV"

run_config() {
    local version=$1 threads=$2 schedule=$3 chunk=$4
    local label chunk_arg log times=() checksum="" first_checksum=""
    label="${version}_t${threads}_${schedule}_${chunk}"
    log="$RAW_DIR/$label.txt"
    chunk_arg=$chunk
    [[ "$chunk" == "default" ]] && chunk_arg=0

    for ((r = 1; r <= RUNS; r++)); do
        local out
        if [[ "$version" == "sequential" ]]; then
            out=$(./bin/nbody_secuencial "$N" "$STEPS")
        else
            out=$(./bin/nbody_paralelo "$N" "$STEPS" --mode "$version" --threads "$threads" \
                --schedule "$schedule" --chunk "$chunk_arg")
        fi
        printf '### run %d\n%s\n\n' "$r" "$out" >>"$log"
        times+=("$(field Elapsed <<<"$out")")
        checksum=$(field Checksum <<<"$out")
        [[ -z "$first_checksum" ]] && first_checksum=$checksum
        if [[ "$checksum" != "$first_checksum" ]]; then
            echo "  aviso: checksum distinto entre corridas en $label" >&2
        fi
    done

    local avg speedup efficiency run_cols
    avg=$(printf '%s\n' "${times[@]}" | awk '{s += $1} END {printf "%.4f", s / NR}')
    [[ "$version" == "sequential" ]] && SEQ_AVG=$avg
    speedup=$(awk -v t1="$SEQ_AVG" -v tp="$avg" 'BEGIN {printf "%.3f", t1 / tp}')
    efficiency=$(awk -v s="$speedup" -v p="$threads" 'BEGIN {printf "%.3f", s / p}')
    run_cols=$(printf '%s,' "${times[@]:0:3}")
    while [[ $(tr -cd ',' <<<"$run_cols" | wc -c) -lt 3 ]]; do run_cols+=","; done

    local schedule_col=$schedule chunk_col=$chunk
    if [[ "$version" == "sequential" ]]; then
        schedule_col=sequential
        chunk_col=-
    fi
    echo "$version,$threads,$schedule_col,$chunk_col,${run_cols}$avg,$speedup,$efficiency,$first_checksum" >>"$CSV"
    printf '%-10s %3s threads  %-7s %-7s  avg %8s s  S=%6s  E=%5s\n' \
        "$version" "$threads" "$schedule_col" "$chunk_col" "$avg" "$speedup" "$efficiency"
}

echo "N=$N steps=$STEPS runs=$RUNS"
run_config sequential 1 none -

CONFIGS=(
    "2 static default"
    "4 static default"
    "8 static default"
    "8 static 8"
    "8 static 64"
    "8 dynamic default"
    "8 dynamic 8"
    "8 dynamic 64"
    "8 guided default"
    "8 guided 8"
    "8 guided 64"
)

for version in parallel optimized; do
    run_config "$version" 1 static default
    for config in "${CONFIGS[@]}"; do
        read -r threads schedule chunk <<<"$config"
        run_config "$version" "$threads" "$schedule" "$chunk"
    done
    for threads in $EXTRA_THREADS; do
        run_config "$version" "$threads" static default
    done
done

run_config atomic 8 static default
run_config atomic 8 dynamic 64

echo
echo "Listo: $CSV y salidas completas en $RAW_DIR/"
