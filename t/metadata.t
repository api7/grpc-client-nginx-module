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

=== TEST 1: unary
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/test.proto", gcli.PROTO_TYPE_FILE))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local res = assert(conn:call("test.Echo", "Metadata", {data = "a"}, {metadata = {
            {"test-k", "v"},
            {"test-repeated", "v1"},
            {"test-repeated", "v2"},
        }}))
        ngx.say(res.data)
        conn:close()
    }
}
--- response_body
map[test-k:[v] test-repeated:[v1 v2]]



=== TEST 2: stream
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/test.proto", gcli.PROTO_TYPE_FILE))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_client_stream("test.ClientStream", "RecvMetadata", {data = "a"}, {metadata = {
            {"test-k", "v"},
            {"test-repeated", "v1"},
            {"test-repeated", "v2"},
        }})
        if not st then
            ngx.say(err)
            return
        end
        local data, err = st:recv_close()
        if not data then
            ngx.say(err)
            return
        end
        ngx.say(data.data)
        conn:close()
    }
}
--- response_body
map[test-k:[v] test-repeated:[v1 v2]]
