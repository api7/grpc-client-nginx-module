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



=== TEST 2: two thread wait for one conn
--- http_config
server {
    listen 2376 http2;

    location / {
        access_by_lua_block {
            ngx.sleep(0.5)
        }
        grpc_pass         grpc://127.0.0.1:2379;
    }
}
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:2376"))
        local function co()
            local ok, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
            if not ok then
                ngx.log(ngx.WARN, err)
            end
        end
        local th1 = ngx.thread.spawn(co)
        local th2 = ngx.thread.spawn(co)
        ngx.thread.wait(th1, th2)
    }
}
--- grep_error_log eval
qr/failed to call: [^,]+/
--- grep_error_log_out
failed to call: busy waiting
