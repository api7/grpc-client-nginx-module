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
	"context"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"os"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/insecure"
)

const (
	ClientStream        int = 1
	ServerStream            = 2
	BidirectionalStream     = 3
)

type ConnectOption struct {
	Insecure       bool
	TLSVerify      bool
	MaxRecvMsgSize int
	ClientCertFile string
	ClientKeyFile  string
	TrustedCA      string
}

type CallOption struct {
	Timeout time.Duration
}

func Connect(target string, opt *ConnectOption) (*grpc.ClientConn, error) {
	opts := []grpc.DialOption{
		// connect timeout
		grpc.WithTimeout(60 * time.Second),
	}

	if opt.MaxRecvMsgSize != 0 {
		opts = append(opts, grpc.WithMaxMsgSize(opt.MaxRecvMsgSize))
	}
	if opt.Insecure {
		opts = append(opts, grpc.WithTransportCredentials(insecure.NewCredentials()))
		return grpc.Dial(target, opts...)
	}

	tc := &tls.Config{}
	if !opt.TLSVerify {
		tc.InsecureSkipVerify = true
	}

	if opt.ClientCertFile != "" && opt.ClientKeyFile != "" {
		// Load the client certificate and its key
		tlsCert, err := tls.LoadX509KeyPair(opt.ClientCertFile, opt.ClientKeyFile)
		if err != nil {
			return nil, err
		}
		if opt.TrustedCA != "" {
			// Load the CA certificate
			trustedCA, err := os.ReadFile(opt.TrustedCA)
			if err != nil {
				return nil, err
			}
			// Put the CA certificate to certificate pool
			caPool := x509.NewCertPool()
			if !caPool.AppendCertsFromPEM(trustedCA) {
				return nil, fmt.Errorf("failed to append trusted certificate to certificate pool. %s", trustedCA)
			}
			tc.RootCAs = caPool
		}
		tc.Certificates = []tls.Certificate{tlsCert}
	}

	opts = append(opts, grpc.WithTransportCredentials(credentials.NewTLS(tc)))

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
	cancel     context.CancelFunc
	streamType int
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
		streamType:   streamType,
		cancel:       cancel,
	}
	return s, nil
}

func (s *Stream) Recv() ([]byte, error) {
	cs := s.ClientStream
	if s.streamType == ClientStream {
		// called by recv_close
		if err := cs.CloseSend(); err != nil {
			return nil, err
		}
	}

	out := &bytes.Buffer{}
	if err := cs.RecvMsg(out); err != nil {
		return nil, err
	}
	return out.Bytes(), nil
}

func (s *Stream) Send(req []byte) (bool, error) {
	cs := s.ClientStream
	if err := cs.SendMsg(req); err != nil {
		return false, err
	}
	return true, nil
}

func (s *Stream) CloseSend() (bool, error) {
	cs := s.ClientStream
	if err := cs.CloseSend(); err != nil {
		return false, err
	}
	return true, nil
}

func (s *Stream) Close() {
	if s.cancel != nil {
		s.cancel()
		s.cancel = nil
	}
}
