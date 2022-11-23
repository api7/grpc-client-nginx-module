-- Copyright 2022 Shenzhen ZhiLiu Technology Co., Ltd.
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
-- http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

package = "grpc-client-nginx-module"
version = "0.3.1-0"
source = {
    url = "git://github.com/api7/grpc-client-nginx-module",
    branch = "v0.3.1",
}

description = {
    summary = "Call gRPC service in Nginx",
    homepage = "https://github.com/api7/grpc-client-nginx-module",
    license = "Apache License 2.0",
    maintainer = "Yuansheng Wang <membphis@gmail.com>"
}

dependencies = {
    "lua-protobuf = 0.3.4",
}


build = {
    type = "builtin",
    modules = {
        ["resty.grpc"] = "lib/resty/grpc.lua",
    }
}
