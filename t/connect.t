use t::GRPC_CLI 'no_plan';

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
