//go:generate protoc --go_out=. --go_opt=paths=source_relative --go-grpc_out=. --go-grpc_opt=paths=source_relative proto/stream.proto
package main

import (
	"io"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/reflection"
	"google.golang.org/grpc/status"

	pb "github.com/api7/grpc-client-nginx-module/t/backend/proto"
)

const (
	grpcAddr = ":50051"
)

type server struct {
	pb.UnimplementedClientStreamServer
	pb.UnimplementedBidirectionalStreamServer
}

func (s *server) Recv(stream pb.ClientStream_RecvServer) error {
	log.Println("client side streaming has been initiated.")
	var count int32 = 0
	totalData := ""
	for {
		req, err := stream.Recv()
		if err == io.EOF {
			log.Printf("send count:%d, data:%s\n", count, totalData)
			return stream.SendAndClose(&pb.RecvResp{Count: count, Data: totalData})
		}
		if err != nil {
			return status.Errorf(codes.Unavailable, "Failed to read client stream: %v", err)
		}

		data := req.GetData()
		totalData += data
		count++
		log.Printf("recv count:%d, data:%s\n", count, totalData)
	}
}

func (s *server) Echo(stream pb.BidirectionalStream_EchoServer) error {
	log.Println("bidirectional streaming has been initiated.")
	var count int32 = 0

	for {
		req, err := stream.Recv()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return status.Errorf(codes.Unavailable, "Failed to read stream: %v", err)
		}

		count++

		if err := stream.Send(&pb.RecvResp{Data: req.GetData(), Count: count}); err != nil {
			return status.Errorf(codes.Unknown, "Failed to stream response back to client: %v", err)
		}
	}
}

func (s *server) EchoSum(stream pb.BidirectionalStream_EchoSumServer) error {
	log.Println("bidirectional streaming has been initiated.")
	var count int32 = 0
	totalData := ""

	for {
		req, err := stream.Recv()
		if err == io.EOF {
			return stream.Send(&pb.RecvResp{Count: count, Data: totalData})
		}
		if err != nil {
			return status.Errorf(codes.Unavailable, "Failed to read stream: %v", err)
		}

		data := req.GetData()
		totalData += data
		count++
	}
}

func main() {
	go func() {
		lis, err := net.Listen("tcp", grpcAddr)
		if err != nil {
			log.Fatalf("failed to listen: %v", err)
		}
		s := grpc.NewServer()
		reflection.Register(s)
		pb.RegisterClientStreamServer(s, &server{})
		pb.RegisterBidirectionalStreamServer(s, &server{})
		if err := s.Serve(lis); err != nil {
			log.Fatalf("failed to serve: %v", err)
		}
	}()

	signals := make(chan os.Signal)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	<-signals
}
