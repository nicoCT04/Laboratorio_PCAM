#!/usr/bin/env bash
set -euo pipefail

N=${1:-20000}
STEPS=${2:-10}
THREADS=${3:-8}

for tool in tmux alacritty grim hyprctl jq magick; do
    command -v "$tool" >/dev/null || { echo "Falta $tool (requiere Hyprland)" >&2; exit 1; }
done

cd "$(dirname "$0")/.."
OUT=evidence
SESSION=pcam-evidence
CLASS=pcam-evidence
mkdir -p "$OUT"
make -s

tmux kill-session -t "$SESSION" 2>/dev/null || true
tmux new-session -d -s "$SESSION" -x 150 -y 32 -c "$PWD"
tmux set-option -t "$SESSION" status off >/dev/null
hyprctl dispatch exec "[float; size 1120 900; center] alacritty --class $CLASS -o font.size=14 -e tmux attach -t $SESSION" >/dev/null

window_json() {
    hyprctl clients -j | jq -c --arg c "$CLASS" 'map(select(.class == $c)) | first // empty'
}

for _ in $(seq 50); do
    [[ -n "$(window_json)" ]] && break
    sleep 0.2
done
[[ -n "$(window_json)" ]] || { echo "No apareció la ventana de la terminal" >&2; exit 1; }
sleep 1

trim_png() {
    local png=$1 background
    background=$(magick "$png" -format '%[pixel:p{3,3}]' info:)
    magick "$png" -fuzz 4% -trim +repage -bordercolor "$background" -border 22 "$png"
}

wait_idle() {
    local shell_pid
    shell_pid=$(tmux display-message -p -t "$SESSION" '#{pane_pid}')
    sleep 0.3
    while pgrep -P "$shell_pid" >/dev/null; do sleep 0.2; done
    sleep 0.4
}

capture() {
    local name=$1
    shift
    tmux send-keys -t "$SESSION" "clear" Enter
    wait_idle
    tmux clear-history -t "$SESSION"
    for cmd in "$@"; do
        tmux send-keys -t "$SESSION" "$cmd" Enter
        wait_idle
    done
    local geometry
    geometry=$(window_json | jq -r '"\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"')
    hyprctl dispatch focuswindow "class:$CLASS" >/dev/null
    sleep 0.3
    grim -g "$geometry" "$OUT/$name.png"
    trim_png "$OUT/$name.png"
    tmux capture-pane -p -J -t "$SESSION" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' >"$OUT/$name.txt"
    echo "$OUT/$name.png"
}

capture compile \
    "make clean && make" \
    "ls -l bin/"

capture sequential \
    "./bin/nbody_secuencial $N $STEPS"

capture parallel \
    "./bin/nbody_paralelo $N $STEPS --mode parallel --threads $THREADS --schedule dynamic --chunk 64"

capture optimized \
    "./bin/nbody_paralelo $N $STEPS --mode optimized --threads $THREADS --schedule dynamic --chunk 64"

capture hardware \
    "LC_ALL=C lscpu | grep -E '^(Model name|CPU\\(s\\)|Thread\\(s\\) per core|Core\\(s\\) per socket):'" \
    "./bin/nbody_paralelo $N 1 --mode parallel --threads $THREADS | grep -E 'Threads|Processors|Max threads'"

capture correctness \
    "./scripts/check_correctness.sh $N $STEPS $THREADS"

tmux kill-session -t "$SESSION"
