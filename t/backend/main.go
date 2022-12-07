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

//go:generate protoc --go_out=. --go_opt=paths=source_relative --go-grpc_out=. --go-grpc_opt=paths=source_relative proto/test.proto
package main

import (
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/signal"
	"strings"
	"syscall"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/reflection"
	"google.golang.org/grpc/status"

	pb "github.com/api7/grpc-client-nginx-module/t/backend/proto"
)

const (
	grpcAddr = ":50051"
)

type server struct {
	pb.UnimplementedEchoServer
	pb.UnimplementedClientStreamServer
	pb.UnimplementedBidirectionalStreamServer
}

func (s *server) Metadata(ctx context.Context, in *pb.RecvReq) (*pb.RecvResp, error) {
	data := ""
	md, ok := metadata.FromIncomingContext(ctx)
	if ok {
		res := map[string][]string{}
		for k, val := range md {
			if strings.HasPrefix(k, "test-") {
				res[k] = val
			}
		}
		data = fmt.Sprintf("%v", res)
	}
	return &pb.RecvResp{
		Data: data,
	}, nil
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

func (s *server) RecvMetadata(stream pb.ClientStream_RecvMetadataServer) error {
	log.Println("client side streaming has been initiated.")
	var count int32 = 0
	totalData := ""
	for {
		md, ok := metadata.FromIncomingContext(stream.Context())
		if ok {
			res := map[string][]string{}
			for k, val := range md {
				if strings.HasPrefix(k, "test-") {
					res[k] = val
				}
			}
			totalData = fmt.Sprintf("%v", res)
		}

		_, err := stream.Recv()
		if err == io.EOF {
			return stream.SendAndClose(&pb.RecvResp{Count: count, Data: totalData})
		}
		if err != nil {
			return status.Errorf(codes.Unavailable, "Failed to read client stream: %v", err)
		}
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
		pb.RegisterEchoServer(s, &server{})
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
