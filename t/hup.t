BEGIN {
    if ($ENV{TEST_NGINX_CHECK_LEAK}) {
        $SkipReason = "unavailable for the hup tests";

    } else {
        $ENV{TEST_NGINX_USE_HUP} = 1;
        undef $ENV{TEST_NGINX_USE_STAP};
    }
}

use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: sanity
--- config
location /t {
    content_by_lua_block {
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
