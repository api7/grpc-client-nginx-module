use t::GRPC_CLI 'no_plan';

log_level("debug");
run_tests();

__DATA__

=== TEST 1: bad target
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local ok, err = pcall(gcli.connect, nil)
        ngx.say(ok)
    }
}
--- response_body
false



=== TEST 2: without close
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 3: close twice
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        local conn = assert(gcli.connect("127.0.0.1:2379"))
        conn:close()
        conn:close()
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 4: call after close
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        conn:close()
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        ngx.say(err)
    }
}
--- response_body
closed



=== TEST 5: call recv max size, normal
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379", {max_recv_msg_size = 100}))
        local _, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        if err then
            return ngx.say(err)
        end
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 6: call recv max size, exceed
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379", {max_recv_msg_size = 5}))
        local _, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        if err then
            return ngx.say(err)
        end
        ngx.say("ok")
    }
}
--- response_body
failed to call: rpc error: code = ResourceExhausted desc = grpc: received message larger than max (28 vs. 5)
