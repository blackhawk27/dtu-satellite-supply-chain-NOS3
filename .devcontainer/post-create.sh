#!/bin/bash
# .devcontainer/post-create.sh

echo "Injecting DTU Tmux Hijack into bashrc..."

cat << 'EOF' >> ~/.bashrc

# --- DTU TMUX HIJACK ---
gnome-terminal() {
    local title="NOS3"
    local cmd=""
    local in_cmd=0
    for arg in "$@"; do
        if [[ "$arg" == --title=* ]]; then
            title="${arg#*=}"
            title="${title//\"/}"
        elif [[ "$arg" == "--" ]]; then
            in_cmd=1
        elif [[ $in_cmd -eq 1 ]]; then
            cmd="$cmd '$arg'"
        fi
    done
    tmux new-window -n "$title" "eval $cmd; read -p 'Enter to close'"
}
export -f gnome-terminal
EOF

echo "Initialization complete."