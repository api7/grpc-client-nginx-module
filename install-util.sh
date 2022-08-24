#!/usr/bin/env bash
set -euo pipefail
set -x

arch=$(uname -m | tr '[:upper:]' '[:lower:]')
if [ "$arch" = "x86_64" ]; then
    arch="amd64"
fi
if [ "$arch" = "aarch64" ]; then
    arch="arm64"
fi

install_go() {
    if grep "NAME=" /etc/os-release | grep  "Alpine"; then
        # the official Go binary is linked with glibc, so we need to download
        # it from the package manager
        apk add --no-cache go
        return
    fi

    GO_VER=1.19
    wget --quiet https://go.dev/dl/go${GO_VER}.linux-${arch}.tar.gz > /dev/null
    rm -rf /usr/local/go && tar -C /usr/local -xzf go${GO_VER}.linux-${arch}.tar.gz
    /usr/local/go/bin/go version
}

case_opt=$1
case "${case_opt}" in
    "install_go")
        install_go
    ;;
    *)
        echo "Unsupported method: ${case_opt}"
    ;;
esac
