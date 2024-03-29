// Copyright 2022 Shenzhen ZhiLiu Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package conn

import (
	"bytes"
	"fmt"

	"google.golang.org/grpc/encoding"
)

// Name is the name registered for the proto compressor.
const Name = "proto"

func init() {
	encoding.RegisterCodec(codec{})
}

// codec is a Codec implementation with protobuf. It is the default codec for gRPC.
type codec struct{}

func (codec) Marshal(v interface{}) ([]byte, error) {
	vv, ok := v.([]byte)
	if !ok {
		return nil, fmt.Errorf("failed to marshal, message is %T, want []byte", v)
	}
	return vv, nil
}

func (codec) Unmarshal(data []byte, v interface{}) error {
	vv, ok := v.(*bytes.Buffer)
	if !ok {
		return fmt.Errorf("failed to unmarshal, message is %T, want *bytes.Buffer", v)
	}
	_, err := vv.Write(data)
	return err
}

func (codec) Name() string {
	return Name
}
