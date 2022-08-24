#!/usr/bin/env bash
set -euo pipefail


install_go() {
    GO_VER=1.19
    wget https://go.dev/dl/go${GO_VER}.linux-amd64.tar.gz
    rm -rf /usr/local/go && tar -C /usr/local -xzf go${GO_VER}.linux-amd64.tar.gz
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
