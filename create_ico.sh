#!/bin/bash
# Convert SVG to ICO for Windows
# Requires ImageMagick: sudo apt install imagemagick (Linux) or choco install imagemagick (Windows)

echo "Converting SVG to ICO..."

# Check if ImageMagick is installed
if ! command -v convert &> /dev/null; then
    echo "Error: ImageMagick is not installed!"
    echo "Install it with:"
    echo "  Ubuntu/Debian: sudo apt install imagemagick"
    echo "  Fedora/RHEL:   sudo dnf install ImageMagick"
    echo "  Arch:          sudo pacman -S imagemagick"
    exit 1
fi

# Choose your source icon (pick the best one)
SOURCE_ICON="resources/icons/monero_rotated_blue.png"
# Alternative: SOURCE_ICON="resources/icons/monero_base.svg"
# Alternative: SOURCE_ICON="resources/icons/wallet.svg"

# Create temporary PNGs at different sizes
convert -background transparent -size 16x16 "$SOURCE_ICON" temp-16.png
convert -background transparent -size 32x32 "$SOURCE_ICON" temp-32.png
convert -background transparent -size 48x48 "$SOURCE_ICON" temp-48.png
convert -background transparent -size 256x256 "$SOURCE_ICON" temp-256.png

# Combine into ICO
convert temp-16.png temp-32.png temp-48.png temp-256.png resources/icons/app.ico

# Cleanup
rm temp-*.png

echo "ICO file created: resources/icons/app.ico"
