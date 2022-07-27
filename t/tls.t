use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: call tls service
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:12379", {insecure = false}))
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(err)
        conn:close()
    }
}
--- response_body eval
qr/certificate has expired or is not yet valid/



=== TEST 2: call tls service, without verify
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:12379", {insecure = false, tls_verify = false}))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(res.header.revision - old)
        conn:close()
    }
}
--- response_body
1
