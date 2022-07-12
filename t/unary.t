use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: sanity
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata", "rpc.proto"))
        local res = gcli.call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(res.header.revision)
    }
}
--- response_body
1
