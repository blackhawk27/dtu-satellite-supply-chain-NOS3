#!/bin/bash
# .devcontainer/post-create.sh

echo "Injecting DTU Tmux Hijack into bashrc..."

cat << 'EOF' >> ~/.bashrc

# --- DTU TMUX HIJACK & WIRETAP ---
gnome-terminal() {
    local title="NOS3"
    local cmd=""
    local in_cmd=0
    for arg in "$@"; do
        if [[ "$arg" == --title=* ]]; then
            title="${arg#*=}"
            title="${title//\"/}"
            # Replace spaces with underscores for clean log file names
            title="${title// /_}" 
        elif [[ "$arg" == "--" ]]; then
            in_cmd=1
        elif [[ $in_cmd -eq 1 ]]; then
            cmd="$cmd '$arg'"
        fi
    done
    
    # Create a directory for our omniscient logs
    mkdir -p /workspaces/dtu-satellite-supply-chain-NOS3/nos3/omni_logs
    
    # Run the command in tmux, but pipe a copy of all output to a log file!
    local log_file="/workspaces/dtu-satellite-supply-chain-NOS3/nos3/omni_logs/${title}.log"
    tmux new-window -n "$title" "eval $cmd 2>&1 | tee '$log_file'; read -p 'Enter to close'"
}
export -f gnome-terminal
EOF

echo "Initialization complete."