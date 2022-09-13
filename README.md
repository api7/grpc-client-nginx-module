# gRPC-client-nginx-module

This module is experimental.

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

After that, install the Lua rock:

luarocks install grpc-client-nginx-module

Finally, setup the thread pool and load the shared library:

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

**syntax:** *res, err = conn:call(service, method, request, callOpt)*

Send a unary request.
The `request` is a Lua table that will be encoded according to the proto.
The `res` is a Lua table that is decoded from the proto message.

callOpt:

* `timeout`: Set the timeout value in milliseconds for the whole call.
60000 milliseconds by default.

### new_server_stream

**syntax:** *stream, err = conn:new_server_stream(service, method, request, callOpt)*

Create a server stream.
The `request` is a Lua table that will be encoded according to the proto.
The `stream` is the server stream which can be used to read the data.

callOpt:

* `timeout`: Set the timeout value in milliseconds for the whole lifetime of
the stream. 60000 milliseconds by default.

#### recv

**syntax:** *res, err = stream:recv()*

Receive a response from the stream.
The `res` is a Lua table that is decoded from the proto message.

### new_client_stream

**syntax:** *stream, err = conn:new_client_stream(service, method, request, callOpt)*

Create a client stream.
The `request` is a Lua table that will be encoded according to the proto.
The `stream` is the client stream which can be used to send/recv the data.

callOpt:

* `timeout`: Set the timeout value in milliseconds for the whole lifetime of
the stream. 60000 milliseconds by default.

#### send

**syntax:** *ok, err = stream:send(request)*

Send a request via the stream.
The `request` is a Lua table that will be encoded according to the proto.

#### recv_close

**syntax:** *res, err = stream:recv_close()*

Receive a response from the stream and close the stream.
The `res` is a Lua table that is decoded from the proto message.

### new_bidirectional_stream

**syntax:** *stream, err = conn:new_bidirectional_stream(service, method, request, callOpt)*

Create a bidirectional stream.
The `request` is a Lua table that will be encoded according to the proto.
The `stream` is the client stream which can be used to send/recv the data.

callOpt:

* `timeout`: Set the timeout value in milliseconds for the whole lifetime of
the stream. 60000 milliseconds by default.

The bidirectional stream has `send` and `recv`, which are equal to the corresponding
version in client/server streams.

## Why don't we

### Why don't we use the gRPC code in Nginx

Because Nginx doesn't provide an interface for the gRPC feature. It requires
we to copy & paste and assemble the components.

### Why don't we compile the gRPC library into the Nginx module

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

## Limitation

* We require this Nginx module runs on 64bit aligned machine, like x64 and arm64.
* resolve `import` in the loaded `.proto` file
(we can handle it like what we have done in Apache APISIX)
* very big integer in int64 can't be displayed correctly due to the missing int64
support in LuaJIT.
