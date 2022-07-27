# gRPC-client-nginx-module

This module is still WIP.

## Install

First of all, build this module into your OpenResty:

```shell
./configure ... \
            --with-threads \
            --add-module=/path/to/grpc-client-nginx-module
```

Then, compile the gRPC engine:

```shell
cd ./grpc-engine &&  go build -o libgrpc_engine.so -buildmode=c-shared main.go
```

After that, setup the thread pool and load the shared library:

```nginx
# Only one background thread is used to communicate with the gRPC engine
thread_pool grpc-client-nginx-module threads=1;
http {
    grpc_client_engine_path /path/to/libgrpc_engine.so;
    ...
}
```

## Synopsis

```lua
access_by_lua_block {
    -- we can't require this library in the init_by_lua
    local gcli = require("resty.grpc")
    assert(gcli.load("t/testdata/rpc.proto")) -- load proto definition into the library
    -- connect the target
    local conn = assert(gcli.connect("127.0.0.1:2379", {insecure = true}))
    -- send unary request
    local res = assert(conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}))
}
```

## Method

### load

**syntax:** *ok, err = resty.grpc.load(proto_file)*

Load the definition of the given proto file, which can be used later

### connect

**syntax:** *conn, err = resty.grpc.connect(target, connectOpt)*

Create a gRPC connection

connectOpt:

* `insecure`: whether connecting the target in an insecure(plaintext) way.
True by default. Set it to false will use TLS connection.
* `tls_verify`: whether to verify the server's TLS certificate

### call

**syntax:** *res, err = conn:call(service, method, request)*

Send a unary request.
The `request` is a Lua table that will be encoded according to the proto.
The `res` is a Lua table that is decoded from the proto message.

## Why don't we compile the gRPC library into the Nginx module

The official gRPC library is written in C++ and requires >2GB dependencies.
We don't use bazel/cmake to build Nginx and downloading >2GB dependencies will
slow down our build process.

Therefore we choose gRPC-go as the core of gRPC engine. If the performance is
an issue, we may consider using Rust instead.

## Debug

Because go's scheduler doesn't work after `fork`, we have to load the Go library
at runtime in each worker.

As a consequence of that, we can't use dlv to debug the process. Therefore we need
to use `log debug`. All logs printed by the std log library will be written to file
`/tmp/grpc-engine-debug.log`.

## TODO

* resolve `import` in the loaded `.proto` file
(we can handle it like what we have done in Apache APISIX)
* very big integer in int64 can't be displayed correctly due to the missing int64
support in LuaJIT.
