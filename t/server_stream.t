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
        local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {key = 'k'})
        if not st then
            ngx.say(err)
            return
        end
        st:close()
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 2: multi streams
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local function co()
            local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {key = 'k'})
            if not st then
                ngx.say(err)
                return
            end
        end
        local ths = {}
        for i = 1, 10 do
            local th = ngx.thread.spawn(co)
            table.insert(ths, th)
        end
        local ok = ngx.thread.wait(unpack(ths))
        ngx.say(ok)
    }
}
--- response_body
true
