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
        local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {create_request = {key = 'k'}})
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
            local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {create_request = {key = 'k'}})
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



=== TEST 3: recv
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local write_conn = assert(gcli.connect("127.0.0.1:2379"))
        local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {create_request = {key = 'k'}}, {timeout = 3000})
        if not st then
            ngx.say(err)
            return
        end

        local function co()
            assert(write_conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'recv'}))
            assert(write_conn:call("etcdserverpb.KV", "DeleteRange", {key = 'k'}))
        end
        ngx.thread.spawn(co)

        local old = assert(st:recv())
        local res = assert(st:recv())
        ngx.say(res.header.revision - old.header.revision)
        ngx.say(res.events[1].type, " ", res.events[1].kv.value)
        local res = assert(st:recv())
        ngx.say(res.events[1].type, " ", res.events[1].kv.key)
    }
}
--- response_body
1
PUT recv
DELETE k



=== TEST 4: multiple watcher
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))

        local function co(st, i)
            local res = assert(st:recv())
            for n = 1, #res.events do
                ngx.log(ngx.WARN, i, ": event type: ", res.events[n].type)
            end
            assert(res.events[1].type == "PUT")

            local ev
            if not res.events[2] then
                res = assert(st:recv())
                ev = res.events[1]
            else
                ev = res.events[2]
            end
            ngx.log(ngx.WARN, i, ": event type: ", ev.type)
            assert(ev.type == "DELETE")
            ngx.log(ngx.WARN, i, ": event on key: ", ev.kv.key)
        end

        local ths = {}
        for i = 1, 5 do
            local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {create_request = {key = 'k'}}, {timeout = 3000})
            if not st then
                ngx.say(err)
                return
            end
            assert(st:recv())
            local th = ngx.thread.spawn(co, st, i)
            table.insert(ths, th)
        end

        assert(conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'recv'}))
        assert(conn:call("etcdserverpb.KV", "DeleteRange", {key = 'k'}))

        local ok = ngx.thread.wait(unpack(ths))
        ngx.say(ok)
    }
}
--- response_body
true
--- grep_error_log eval
qr/event on key: \w+/
--- grep_error_log_out
event on key: k
event on key: k
event on key: k
event on key: k
event on key: k



=== TEST 5: lifetime timeout
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local write_conn = assert(gcli.connect("127.0.0.1:2379"))
        local st, err = conn:new_server_stream("etcdserverpb.Watch", "Watch", {create_request = {key = 'k'}}, {timeout = 100})
        if not st then
            ngx.say(err)
            return
        end

        assert(st:recv())
        local res, err = st:recv()
        ngx.say(err)
    }
}
--- response_body eval
qr/(context deadline exceeded|timeout)/
