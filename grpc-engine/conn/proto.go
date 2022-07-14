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
