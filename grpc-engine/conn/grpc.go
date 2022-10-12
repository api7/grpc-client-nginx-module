package conn

import (
	"bytes"
	"context"
	"crypto/tls"
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
}

type CallOption struct {
	Timeout time.Duration
}

func newCert(certfile, keyfile string) (*tls.Certificate, error) {
	cert, err := os.ReadFile(certfile)
	if err != nil {
		return nil, err
	}

	key, err := os.ReadFile(keyfile)
	if err != nil {
		return nil, err
	}

	tlsCert, err := tls.X509KeyPair(cert, key)
	if err != nil {
		return nil, err
	}
	return &tlsCert, nil
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

		if (opt.ClientKeyFile == "") != (opt.ClientCertFile == "") {
			return nil, fmt.Errorf("ClientKeyFile and ClientCertFile must both be present or both absent: key: %v, cert: %v", opt.ClientKeyFile, opt.ClientCertFile)
		}
		if opt.ClientCertFile != "" {
			certificate, err := newCert(opt.ClientCertFile, opt.ClientKeyFile)
			if err != nil {
				return nil, err
			}
			tc.Certificates = []tls.Certificate{*certificate}
		}

		if !opt.TLSVerify {
			tc.InsecureSkipVerify = true
		}

		opts = append(opts, grpc.WithTransportCredentials(credentials.NewTLS(tc)))
	}

	if opt.MaxRecvMsgSize != 0 {
		opts = append(opts, grpc.WithMaxMsgSize(opt.MaxRecvMsgSize))
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
