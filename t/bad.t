# incorrect use case
use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: mix req
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
        ngx.say(err)
        conn:close()
    }
}
--- response_body
bad request
