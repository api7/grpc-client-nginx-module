use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: sanity
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "Echo", {data = "a"})
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


=== TEST 2: send & recv
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "Echo", {data = "a"})
        if not st then
            ngx.say(err)
            return
        end
        local data, err = st:recv()
        if not data then
            ngx.say(err)
            return
        end
        ngx.say(data.count)
        ngx.say(data.data)
    }
}
--- response_body
1
a



=== TEST 3: multi send & recv
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "Echo", {data = "a"})
        if not st then
            ngx.say(err)
            return
        end
        local data, err = st:recv()
        if not data then
            ngx.say(err)
            return
        end
        ngx.say(data.count)
        ngx.say(data.data)

        local sum = ""
        local sum_count = 0
        for i = 1, 4 do
            assert(st:send({data = tostring(i)}))
            local data, err = st:recv()
            if not data then
                ngx.say(err)
                return
            end

            sum = sum .. data.data
            sum_count = sum_count + data.count
        end
        ngx.say(sum_count)
        ngx.say(sum)
    }
}
--- response_body
1
a
14
1234



=== TEST 4: send & close_send & recv
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/backend/proto/stream.proto"))

        local conn = assert(gcli.connect("127.0.0.1:50051"))
        local st, err = conn:new_bidirectional_stream("stream.BidirectionalStream", "EchoSum", {data = "a"})
        if not st then
            ngx.say(err)
            return
        end
        for i = 1, 4 do
            assert(st:send({data = tostring(i)}))
        end
        assert(st:close_send())
        local data, err = st:recv()
        if not data then
            ngx.say(err)
            return
        end
        ngx.say(data.count)
        ngx.say(data.data)
    }
}
--- response_body
5
a1234
