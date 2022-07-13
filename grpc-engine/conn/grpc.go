package conn

import (
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

func Connect() *grpc.ClientConn {
	conn, err := grpc.Dial("localhost:2379",
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		// connect timeout
		grpc.WithTimeout(60*time.Second),
	)
	if err != nil {
		panic(err)
	}

	return conn
}

func Close(c *grpc.ClientConn) {
	// ignore close err
	c.Close()
}
