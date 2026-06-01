#!/bin/bash
OS=$(uname -s)
ARCH=$(uname -m)

if [ "$OS" = "Linux" ]; then
    if [ "$ARCH" = "aarch64" ]; then
        # Run the ARM build of the image processor.
        exec /mirror/img_processor/imgprocP_linux_arm "$@"
    elif [ "$ARCH" = "x86_64" ]; then
        # Run the x86_64 build of the image processor.
        exec /mirror/img_processor/imgprocP_linux_x86 "$@"
    else
        echo "Unsupported Linux architecture: $ARCH"
        exit 1
    fi
else
    echo "Unknown OS: $OS"
    exit 1
fi
