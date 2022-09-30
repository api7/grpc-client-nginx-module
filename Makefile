OPENRESTY_PREFIX ?= /usr/local/openresty
INSTALL ?= install

.PHONY: install
install:
	if [ ! -f /usr/local/go/bin/go ]; then ./install-util.sh install_go; fi
	cd ./grpc-engine && PATH="$(PATH):/usr/local/go/bin" go build -o libgrpc_engine.so -buildmode=c-shared main.go
	$(INSTALL) -m 664 ./grpc-engine/libgrpc_engine.so $(OPENRESTY_PREFIX)/
	$(INSTALL) -m 664 lib/resty/*.lua $(OPENRESTY_PREFIX)/lualib/resty/
