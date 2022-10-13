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
