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

OPENRESTY_PREFIX ?= /usr/local/openresty
INSTALL ?= install

.PHONY: install
install:
	if [ ! -f /usr/local/go/bin/go ]; then ./install-util.sh install_go; fi
	cd ./grpc-engine && PATH="$(PATH):/usr/local/go/bin" go build -o libgrpc_engine.so -buildmode=c-shared main.go
	$(INSTALL) -m 664 ./grpc-engine/libgrpc_engine.so $(OPENRESTY_PREFIX)/
	$(INSTALL) -m 664 lib/resty/*.lua $(OPENRESTY_PREFIX)/lualib/resty/
