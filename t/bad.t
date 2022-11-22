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

# incorrect use case
use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: call in non-yield phases
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        package.loaded.conn = conn
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(err)
    }
    header_filter_by_lua_block {
        local conn = package.loaded.conn
        local ok, err = conn:call("etcdserverpb.KV", "Range", {key = 'k'})
        ngx.log(ngx.WARN, "failed to call: ", err)
        conn:close()
    }
}
--- response_body
nil
--- error_log
API disabled in the context of header_filter_by_lua*
