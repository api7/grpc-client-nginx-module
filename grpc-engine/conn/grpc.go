package conn

import (
	"bytes"
	"context"
	"crypto/tls"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/insecure"
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
