#!/bin/bash
# Setup script for SmartSpin2k external dependencies
# This script clones the required external Arduino libraries into the components directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPONENTS_DIR="$SCRIPT_DIR/components"

echo "SmartSpin2k Dependency Setup"
echo "=============================="
echo ""
echo "This script will clone external dependencies into the components/ directory."
echo ""

# Create components directory if it doesn't exist
mkdir -p "$COMPONENTS_DIR"

# Function to clone or update a repository
clone_or_update() {
    local name=$1
    local url=$2
    local branch=$3
    local target_dir="$COMPONENTS_DIR/$name"
    
    if [ -d "$target_dir" ]; then
        echo "[$name] Already exists, updating..."
        cd "$target_dir"
        git fetch
        if [ -n "$branch" ]; then
            git checkout "$branch"
            git pull
        else
            git pull
        fi
        cd "$SCRIPT_DIR"
    else
        echo "[$name] Cloning from $url..."
        if [ -n "$branch" ]; then
            git clone --branch "$branch" --depth 1 "$url" "$target_dir"
        else
            git clone --depth 1 "$url" "$target_dir"
        fi
    fi
    echo "[$name] Done!"
    echo ""
}

# Clone external dependencies
echo "Fetching external dependencies..."
echo ""

clone_or_update "esp-nimble-cpp" "https://github.com/doudar/esp-nimble-cpp.git" ""
clone_or_update "TMCStepper" "https://github.com/teemuatlut/TMCStepper.git" "v0.7.3"
clone_or_update "ArduinoJson" "https://github.com/bblanchon/ArduinoJson.git" "v7.3.1"
clone_or_update "FastAccelStepper" "https://github.com/doudar/FastAccelStepper.git" ""
clone_or_update "ArduinoWebsockets" "https://github.com/doudar/ArduinoWebsockets.git" ""

echo "=============================="
echo "All dependencies installed!"
echo ""
echo "Next steps:"
echo "  1. Set target: idf.py set-target esp32"
echo "  2. Build: idf.py build"
echo "  3. Flash: idf.py flash"
echo ""
echo "See BUILDING.md for more detailed instructions."
