# gRPC-client-nginx-module

This module is experimental.

## Install

First of all, build this module into your OpenResty:

```shell
./configure ... \
            --with-threads \
            --with-cc-opt="-DNGX_GRPC_CLI_ENGINE_PATH=/path/to/libgrpc_engine.so" \
            --add-module=/path/to/grpc-client-nginx-module
```

We need to specify the path of engine via build argument "-DNGX_GRPC_CLI_ENGINE_PATH".

Then, compile the gRPC engine:

```shell
cd ./grpc-engine &&  go build -o libgrpc_engine.so -buildmode=c-shared main.go
```

After that, install the Lua rock:

luarocks install grpc-client-nginx-module

Make sure the Lua rock version matches the tag version of the Nginx module.

Finally, setup the thread pool:

```nginx
# Only one background thread is used to communicate with the gRPC engine
thread_pool grpc-client-nginx-module threads=1;
http {
    ...
}
stream {
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

This module can be run in stream subsystem too:

```lua
preread_by_lua_block {
    local gcli = require("resty.grpc")
    assert(gcli.load("t/testdata/rpc.proto"))

    local conn = assert(gcli.connect("127.0.0.1:2379"))
    local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
    conn:close()
}
return '';
```

## Method

### load

**syntax:** *ok, err = resty.grpc.load(proto[, proto_type])*

Load the definition of the given proto, which can be used later.
The `proto_type` can be one of:

* `PROTO_TYPE_FILE`
* `PROTO_TYPE_STR`

For instance, `grpc.load("t/testdata/rpc.proto", grpc.PROTO_TYPE_FILE)`

If not given, `PROTO_TYPE_FILE` will be used by default.

### connect

**syntax:** *conn, err = resty.grpc.connect(target, connectOpt)*

Create a gRPC connection

connectOpt:

* `insecure`: whether connecting the target in an insecure(plaintext) way.
True by default. Set it to false will use TLS connection.
* `tls_verify`: whether to verify the server's TLS certificate.
* `max_recv_msg_size`: sets the maximum message size in bytes the client can receive.
* `client_cert`: the path of certificate used in client certificate verification.
* `client_key`: the path of key used in client certificate verification. The key should match the given certificate.
* `trusted_ca`: the path of trusted certificate.

### call

**syntax:** *res, err = conn:call(service, method, request, callOpt)*

Send a unary request.
The `request` is a Lua table that will be encoded according to the proto.
The `res` is a Lua table that is decoded from the proto message.

callOpt:

* `timeout`: Set the timeout value in milliseconds for the whole call.
60000 milliseconds by default.
* `int64_encoding`: Set the eocoding for int64. The value can be one of:
  * INT64_AS_NUMBER: set value to integer when it fit into uint32, otherwise return a number **(default)**
  * INT64_AS_STRING: same as above, but return a string instead
  * INT64_AS_HEXSTRING: same as above, but return a hexadigit string instead

For example,

```
conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'},
            {int64_encoding = gcli.INT64_AS_STRING})
```

will decode int64 result in `#number`.

*Note*: The string returned by `int64_as_string` or `int64_as_hexstring` will prefix a `'#'` character.
This behavior is done in `lua-protobuf`.

### new_server_stream

**syntax:** *stream, err = conn:new_server_stream(service, method, request, callOpt)*

Create a server stream.
The `request` is a Lua table that will be encoded according to the proto.
The `stream` is the server stream which can be used to read the data.

callOpt:

* `timeout`: Set the timeout value in milliseconds for the whole lifetime of
the stream. 60000 milliseconds by default.
* `int64_encoding`: Set the eocoding for int64.

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
* `int64_encoding`: Set the eocoding for int64.

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
* `int64_encoding`: Set the eocoding for int64.

The bidirectional stream has `send` and `recv`, which are equal to the corresponding
version in client/server streams.

#### close_send

**syntax:** *ok, err = stream:close_send()*

Close the send side of the bidirectional stream.

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
