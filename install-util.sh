#!/usr/bin/env bash
# Copyright 2022 Shenzhen ZhiLiu Technology Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
