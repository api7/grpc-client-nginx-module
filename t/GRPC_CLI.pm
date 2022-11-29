# Copyright 2022 Shenzhen ZhiLiu Technology Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

package t::GRPC_CLI;

use Test::Nginx::Socket::Lua;
use Test::Nginx::Socket::Lua::Stream -Base;
use Cwd qw(cwd);

log_level("info");
no_long_string();
no_shuffle();
master_on();
worker_connections(128);


$ENV{TEST_NGINX_HTML_DIR} ||= html_dir();

add_block_preprocessor(sub {
    my ($block) = @_;

    if (!$block->no_error_log && !$block->error_log && !$block->grep_error_log) {
        $block->set_value("no_error_log", "[error]\n[alert]\nERROR: AddressSanitizer");
    }

    if (!$block->no_shutdown_error_log) {
        $block->set_value("no_shutdown_error_log", "LeakSanitizer");
    }

    if (defined $block->stream_server_config) {
        my $stream_config = $block->stream_config // '';
        $stream_config .= <<_EOC_;
        lua_package_path "lib/?.lua;;";
_EOC_

        $block->set_value("stream_config", $stream_config);

        my $main_config = $block->main_config // '';
        $main_config .= <<_EOC_;
        thread_pool grpc-client-nginx-module threads=1;
_EOC_

        $block->set_value("main_config", $main_config);
    }

    if (defined $block->config) {
        if (!$block->request) {
            $block->set_value("request", "GET /t");
        }

        my $http_config = $block->http_config // '';
        $http_config .= <<_EOC_;
        lua_package_path "lib/?.lua;;";
_EOC_

        $block->set_value("http_config", $http_config);

        my $main_config = $block->main_config // '';
        $main_config .= <<_EOC_;
        thread_pool grpc-client-nginx-module threads=1;
_EOC_

        $block->set_value("main_config", $main_config);
    }
});

1;
