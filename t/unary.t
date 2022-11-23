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

use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: sanity
--- log_level: debug
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto", gcli.PROTO_TYPE_FILE))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(res.header.revision - old)
        conn:close()
    }
}
--- response_body
1
--- grep_error_log eval
qr/(create|close) gRPC connection/
--- grep_error_log_out
create gRPC connection
close gRPC connection



=== TEST 2: call repeatedly
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        for i = 1, 3 do
            assert(conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}))
            local res, err = conn:call("etcdserverpb.KV", "Range", {key = 'k', range_end = 'ka', revision = 0})
            if not res then
                ngx.log(ngx.ERR, err)
                return
            end
            local kv = res.kvs[1]
            if kv == nil then
                return
            end
            ngx.say(kv.key, " ", kv.value)
            local res, err = conn:call("etcdserverpb.KV", "DeleteRange", {key = 'k', range_end = 'ka'})
            if not res then
                ngx.log(ngx.ERR, err)
                return
            end
            ngx.say(res.deleted)
        end
    }
}
--- response_body
k v
1
k v
1
k v
1



=== TEST 3: not yieldable
--- config
location /t {
    return 200;
    header_filter_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local ok, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.log(ngx.ERR, "failed to call: ", err)
    }
}
--- error_log
failed to call: API disabled in the context of



=== TEST 4: connect refused
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:1979"))
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        if not res then
            ngx.say(err)
        end
    }
}
--- response_body eval
qr/connect: connection refused/



=== TEST 5: service not found
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res, err = conn:call("etcdserverpb.KVs", "Put", {key = 'k', value = 'v'})
        if not res then
            ngx.say(err)
        end
    }
}
--- response_body
service etcdserverpb.KVs not found



=== TEST 6: method not found
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res, err = conn:call("etcdserverpb.KV", "Puts", {key = 'k', value = 'v'})
        if not res then
            ngx.say(err)
        end
    }
}
--- response_body
method Puts not found



=== TEST 7: failed to encode
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 1, value = 'v'})
        if not res then
            ngx.say(err)
        end
    }
}
--- response_body
failed to encode: bad argument #2 to '?' (string expected for field 'key', got number)



=== TEST 8: wrong request proto
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/bad.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 1})
        if not res then
            ngx.say(err)
        end
    }
}
--- response_body eval
qr/error unmarshalling request/



=== TEST 9: wrong response proto
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/bad.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res, err = conn:call("etcdserverpb.KV", "Range", {key = 'k'})
        if not res then
            ngx.say(err)
        end
    }
}
--- response_body
failed to decode: type mismatch for field 'key' at offset 4, bytes expected for type bytes, got varint



=== TEST 10: resume before content_by_lua
--- config
location /t {
    access_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(res.header.revision - old)
        conn:close()
    }
}
--- response_body
1



=== TEST 11: not insecure
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))
        local conn = assert(gcli.connect("127.0.0.1:2379", {insecure = false}))
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(err)
    }
}
--- response_body eval
qr/authentication handshake failed/



=== TEST 12: default insecure
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(err)
    }
}
--- response_body
nil



=== TEST 13: with timeout
--- log_level: debug
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local opt = {timeout = 1.1 * 1000}
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}, opt)
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'}, opt)
        ngx.say(res.header.revision - old)
        conn:close()
    }
}
--- response_body
1
--- grep_error_log eval
qr/yield gRPC ctx:\w+, timeout:\d+/
--- grep_error_log_out eval
qr/yield gRPC ctx:\w+, timeout:1100
yield gRPC ctx:\w+, timeout:1100/



=== TEST 14: timed out
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(4)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local opt = {timeout = 0.1 * 1000}
        local conn = assert(gcli.connect("127.0.0.1:2376"))
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}, opt)
        ngx.say(err)
        conn:close()
    }
}
--- response_body
failed to call: timeout



=== TEST 15: call with delay
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(1)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local opt = {timeout = 1.1 * 1000}
        local conn = assert(gcli.connect("127.0.0.1:2376"))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}, opt)
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(res.header.revision - old)
        conn:close()
    }
}
--- response_body
1



=== TEST 16: close before finishing
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(0.5)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2376"))
        local function co()
            conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        end
        local th = ngx.thread.spawn(co)
        conn:close()
        ngx.say('ok')
    }
}
--- response_body
ok



=== TEST 17: close before timeout
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(0.5)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2376"))
        local function co()
            local opt = {timeout = 100}
            conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}, opt)
        end
        local th = ngx.thread.spawn(co)
        conn:close()
        ngx.say('ok')
    }
}
--- response_body
ok



=== TEST 18: abort
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(100)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2376"))
        local opt = {timeout = 1000}
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}, opt)
        ngx.log(ngx.ERR, "unreacheable")
    }
}
--- timeout: 0.2
--- abort
--- wait: 0.7
--- ignore_response



=== TEST 19: mix req
--- http_config
init_worker_by_lua_block {
    local gcli = require("resty.grpc")
    assert(gcli.load("t/testdata/rpc.proto"))

    package.loaded.conn = assert(gcli.connect("127.0.0.1:2379"))
}
--- config
location /t {
    content_by_lua_block {
        local conn = package.loaded.conn
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(res.header.revision - old)
        conn:close()
    }
}
--- response_body
1



=== TEST 20: many threads wait for one conn
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(0.1)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2376"))
        assert(conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}))
        local function co()
            local res, err = conn:call("etcdserverpb.KV", "Range", {key = 'k'})
            if not res then
                ngx.log(ngx.ERR, err)
                return
            end
            local kv = res.kvs[1]
            if kv == nil then
                return
            end
            if kv.key ~= "k" or kv.value ~= "v" then
                ngx.log(ngx.ERR, "k: ", kv.key, ", v: ", kv.value)
            end
        end
        local ths = {}
        for i = 1, 10 do
            local th = ngx.thread.spawn(co)
            table.insert(ths, th)
        end
        local ok = ngx.thread.wait(unpack(ths))
        ngx.say(ok)
    }
}
--- response_body
true



=== TEST 21: mix error in different req
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/bad.proto"))
        local conn1 = assert(gcli.connect("127.0.0.1:2379"))
        local conn2 = assert(gcli.connect("127.0.0.1:2379", {insecure = false}))

        local function co1()
            local res, err = conn1:call("etcdserverpb.KV", "Put", {key = 1})
            if not res then
                ngx.log(ngx.WARN, err)
            end
        end
        local function co2()
            local res, err = conn2:call("etcdserverpb.KV", "Put", {key = 1})
            if not res then
                ngx.log(ngx.WARN, err)
            end
        end
        local ths = {}
        for i = 1, 10 do
            local th
            if i % 2 == 1 then
                th = ngx.thread.spawn(co1)
            else
                th = ngx.thread.spawn(co2)
            end
            table.insert(ths, th)
        end
        local ok = ngx.thread.wait(unpack(ths))
        ngx.say(ok)
    }
}
--- grep_error_log eval
qr/error: desc = "transport: authentication handshake failed: EOF",|error unmarshalling request: proto: wrong wireType = 0 for field Key,/
--- grep_error_log_out eval
qr/(error: desc = "transport: authentication handshake failed: EOF",|error unmarshalling request: proto: wrong wireType = 0 for field Key,)\n{1,5}/



=== TEST 22: called in init_worker_by_lua_block
--- http_config
init_worker_by_lua_block {
    local gcli = require("resty.grpc")
    assert(gcli.load("t/testdata/rpc.proto"))

    local conn = assert(gcli.connect("127.0.0.1:2379"))
    local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
    local old = res.header.revision
    local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
    package.loaded.diff = res.header.revision - old
}
--- config
location /t {
    content_by_lua_block {
        ngx.say(package.loaded.diff)
    }
}
--- response_body
1



=== TEST 23: called in init_worker_by_lua_block, something wrong happened
--- http_config
init_worker_by_lua_block {
    local gcli = require("resty.grpc")
    assert(gcli.load("t/testdata/rpc.proto"))

    local conn = assert(gcli.connect("127.0.0.1:2376"))
    local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
    package.loaded.err = err
}
--- config
location /t {
    content_by_lua_block {
        ngx.say(package.loaded.err)
    }
}
--- response_body eval
qr/connect: connection refused/



=== TEST 24: called in init_worker_by_lua_block, timeout
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(4)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
init_worker_by_lua_block {
    local gcli = require("resty.grpc")
    assert(gcli.load("t/testdata/rpc.proto"))

    local conn = assert(gcli.connect("127.0.0.1:2376"))
    local opt = {timeout = 100}
    local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'}, opt)
    package.loaded.err = err
}
--- config
location /t {
    content_by_lua_block {
        ngx.say(package.loaded.err)
    }
}
--- response_body eval
qr/context deadline exceeded/
