package conn

import (
	"bytes"
	"context"
	"crypto/tls"
	"fmt"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/insecure"
)

const (
	ClientStream int = iota
	ServerStream
	BidirectionalStream
)

type ConnectOption struct {
	Insecure  bool
	TLSVerify bool
}

type CallOption struct {
	Timeout time.Duration
}

func Connect(target string, opt *ConnectOption) (*grpc.ClientConn, error) {
	opts := []grpc.DialOption{
		// connect timeout
		grpc.WithTimeout(60 * time.Second),
	}

	if opt.Insecure {
		opts = append(opts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	} else {
		tc := &tls.Config{}

		if !opt.TLSVerify {
			tc.InsecureSkipVerify = true
		}

		opts = append(opts, grpc.WithTransportCredentials(credentials.NewTLS(tc)))
	}

	conn, err := grpc.Dial(target, opts...)
	if err != nil {
		return nil, err
	}

	return conn, nil
}

func Close(c *grpc.ClientConn) {
	// ignore close err
	c.Close()
}

func Call(c *grpc.ClientConn, method string, req []byte, opt *CallOption) ([]byte, error) {
	ctx := context.Background()
	var cancel context.CancelFunc
	if opt.Timeout > 0 {
		ctx, cancel = context.WithTimeout(ctx, opt.Timeout)
		defer cancel()
	}

	out := &bytes.Buffer{}
	err := c.Invoke(ctx, method, req, out)
	if err != nil {
		return nil, err
	}
	return out.Bytes(), nil
}

type Stream struct {
	grpc.ClientStream
	cancel context.CancelFunc
}

func NewStream(c *grpc.ClientConn, method string, req []byte, opt *CallOption, streamType int) (*Stream, error) {
	ctx := context.Background()
	var cancel context.CancelFunc
	if opt.Timeout > 0 {
		ctx, cancel = context.WithTimeout(ctx, opt.Timeout)
	}

	desc := &grpc.StreamDesc{}
	switch streamType {
	case ClientStream:
		desc.ClientStreams = true
	case ServerStream:
		desc.ServerStreams = true
	case BidirectionalStream:
		desc.ClientStreams = true
		desc.ServerStreams = true
	default:
		panic(fmt.Sprintf("Unknown stream type: %d", streamType))
	}

	cs, err := c.NewStream(ctx, desc, method)
	if err != nil {
		cancel()
		return nil, err
	}
	if err := cs.SendMsg(req); err != nil {
		cancel()
		return nil, err
	}

	if streamType == ServerStream {
		// TODO: non-ServerStream don't work like this
		if err := cs.CloseSend(); err != nil {
			cancel()
			return nil, err
		}
	}

	s := &Stream{
		ClientStream: cs,
		cancel:       cancel,
	}
	return s, nil
}

func (s *Stream) Recv() ([]byte, error) {
	cs := s.ClientStream
	out := &bytes.Buffer{}
	if err := cs.RecvMsg(out); err != nil {
		return nil, err
	}
	return out.Bytes(), nil
}

func (s *Stream) Close() {
	if s.cancel != nil {
		s.cancel()
		s.cancel = nil
	}
}
