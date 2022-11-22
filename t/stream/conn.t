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
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local ok, err = pcall(gcli.connect, nil)
        ngx.say(ok)
    }
--- stream_response
false



=== TEST 2: without close
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        ngx.say("ok")
    }
--- stream_response
ok



=== TEST 3: close twice
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        conn:close()
        conn:close()
        ngx.say("ok")
    }
--- stream_response
ok



=== TEST 4: call after close
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        conn:close()
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(err)
    }
--- stream_response
closed
