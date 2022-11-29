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

log_level("debug");
run_tests();

__DATA__

=== TEST 1: bad target
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local ok, err = pcall(gcli.connect, nil)
        ngx.say(ok)
    }
}
--- response_body
false



=== TEST 2: without close
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 3: close twice
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        conn:close()
        conn:close()
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 4: call after close
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        conn:close()
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(err)
    }
}
--- response_body
closed



=== TEST 5: call recv max size, normal
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379", {max_recv_msg_size = 100}))
        local _, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        if err then
            return ngx.say(err)
        end
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 6: call recv max size, exceed
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379", {max_recv_msg_size = 5}))
        local _, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        if err then
            return ngx.say(err)
        end
        ngx.say("ok")
    }
}
--- response_body
failed to call: rpc error: code = ResourceExhausted desc = grpc: received message larger than max (28 vs. 5)



=== TEST 7: load str
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local f = io.open("t/testdata/rpc.proto")
        local content = f:read("*a")
        f:close()
        assert(gcli.load(content, gcli.PROTO_TYPE_STR))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(res.header.revision - old)
    }
}
--- response_body
1



=== TEST 8: int64_as_string/number/hexstring
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'},
            {int64_encoding = gcli.INT64_AS_STRING})
        ngx.say(type(res.header.member_id))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(type(res.header.member_id))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'},
            {int64_encoding = gcli.INT64_AS_NUMBER})
        ngx.say(type(res.header.member_id))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'},
            {int64_encoding = gcli.INT64_AS_HEXSTRING})
        ngx.say(type(res.header.member_id))
        conn:close()
    }
}
--- response_body
string
number
number
string



=== TEST 9: stream closed during processing
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            if package.loaded.count == nil then
                package.loaded.count = 0
            end

            local count = package.loaded.count
            if count == 1 then
                package.loaded.count = package.loaded.count + 1
                ngx.exit(499)
            end
            package.loaded.count = package.loaded.count + 1
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
            local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {create_request = {key = 'k'}})
            if not st then
                ngx.say(err)
                return
            end

            local ok, err = st:recv()
            if not ok then
                ngx.log(ngx.ERR, err)
                return
            end
        end
        local ths = {}
        for i = 1, 3 do
            co()
        end
        ngx.say("true")
    }
}
--- response_body
true
--- grep_error_log eval
qr/failed to recv: rpc error: code = Internal desc = stream terminated/
--- grep_error_log_out
failed to recv: rpc error: code = Internal desc = stream terminated
