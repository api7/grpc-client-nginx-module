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
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "Echo", {data = "a"})
        if not st then
            ngx.say(err)
            return
        end
        st:close()
        ngx.say("ok")
    }
--- stream_response
ok


=== TEST 2: send & recv
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "Echo", {data = "a"})
        if not st then
            ngx.say(err)
            return
        end
        local data, err = st:recv()
        if not data then
            ngx.say(err)
            return
        end
        ngx.say(data.count)
        ngx.say(data.data)
    }
--- stream_response
1
a



=== TEST 3: multi send & recv
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "Echo", {data = "a"})
        if not st then
            ngx.say(err)
            return
        end
        local data, err = st:recv()
        if not data then
            ngx.say(err)
            return
        end
        ngx.say(data.count)
        ngx.say(data.data)

        local sum = ""
        local sum_count = 0
        for i = 1, 4 do
            assert(st:send({data = tostring(i)}))
            local data, err = st:recv()
            if not data then
                ngx.say(err)
                return
            end

            sum = sum .. data.data
            sum_count = sum_count + data.count
        end
        ngx.say(sum_count)
        ngx.say(sum)
    }
--- stream_response
1
a
14
1234



=== TEST 4: send & close_send & recv
--- stream_server_config
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "EchoSum", {data = "a"})
        if not st then
            ngx.say(err)
            return
        end
        for i = 1, 4 do
            assert(st:send({data = tostring(i)}))
        end
        assert(st:close_send())
        local data, err = st:recv()
        if not data then
            ngx.say(err)
            return
        end
        ngx.say(data.count)
        ngx.say(data.data)
    }
--- stream_response
5
a1234
