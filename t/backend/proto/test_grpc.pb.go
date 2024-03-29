// Code generated by protoc-gen-go-grpc. DO NOT EDIT.

package proto

import (
	context "context"
	grpc "google.golang.org/grpc"
	codes "google.golang.org/grpc/codes"
	status "google.golang.org/grpc/status"
)

// This is a compile-time assertion to ensure that this generated file
// is compatible with the grpc package it is being compiled against.
// Requires gRPC-Go v1.32.0 or later.
const _ = grpc.SupportPackageIsVersion7

// EchoClient is the client API for Echo service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://pkg.go.dev/google.golang.org/grpc/?tab=doc#ClientConn.NewStream.
type EchoClient interface {
	Metadata(ctx context.Context, in *RecvReq, opts ...grpc.CallOption) (*RecvResp, error)
}

type echoClient struct {
	cc grpc.ClientConnInterface
}

func NewEchoClient(cc grpc.ClientConnInterface) EchoClient {
	return &echoClient{cc}
}

func (c *echoClient) Metadata(ctx context.Context, in *RecvReq, opts ...grpc.CallOption) (*RecvResp, error) {
	out := new(RecvResp)
	err := c.cc.Invoke(ctx, "/test.Echo/Metadata", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

// EchoServer is the server API for Echo service.
// All implementations must embed UnimplementedEchoServer
// for forward compatibility
type EchoServer interface {
	Metadata(context.Context, *RecvReq) (*RecvResp, error)
	mustEmbedUnimplementedEchoServer()
}

// UnimplementedEchoServer must be embedded to have forward compatible implementations.
type UnimplementedEchoServer struct {
}

func (UnimplementedEchoServer) Metadata(context.Context, *RecvReq) (*RecvResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method Metadata not implemented")
}
func (UnimplementedEchoServer) mustEmbedUnimplementedEchoServer() {}

// UnsafeEchoServer may be embedded to opt out of forward compatibility for this service.
// Use of this interface is not recommended, as added methods to EchoServer will
// result in compilation errors.
type UnsafeEchoServer interface {
	mustEmbedUnimplementedEchoServer()
}

func RegisterEchoServer(s grpc.ServiceRegistrar, srv EchoServer) {
	s.RegisterService(&Echo_ServiceDesc, srv)
}

func _Echo_Metadata_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(RecvReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(EchoServer).Metadata(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/test.Echo/Metadata",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(EchoServer).Metadata(ctx, req.(*RecvReq))
	}
	return interceptor(ctx, in, info, handler)
}

// Echo_ServiceDesc is the grpc.ServiceDesc for Echo service.
// It's only intended for direct use with grpc.RegisterService,
// and not to be introspected or modified (even as a copy)
var Echo_ServiceDesc = grpc.ServiceDesc{
	ServiceName: "test.Echo",
	HandlerType: (*EchoServer)(nil),
	Methods: []grpc.MethodDesc{
		{
			MethodName: "Metadata",
			Handler:    _Echo_Metadata_Handler,
		},
	},
	Streams:  []grpc.StreamDesc{},
	Metadata: "proto/test.proto",
}

// ClientStreamClient is the client API for ClientStream service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://pkg.go.dev/google.golang.org/grpc/?tab=doc#ClientConn.NewStream.
type ClientStreamClient interface {
	Recv(ctx context.Context, opts ...grpc.CallOption) (ClientStream_RecvClient, error)
	RecvMetadata(ctx context.Context, opts ...grpc.CallOption) (ClientStream_RecvMetadataClient, error)
}

type clientStreamClient struct {
	cc grpc.ClientConnInterface
}

func NewClientStreamClient(cc grpc.ClientConnInterface) ClientStreamClient {
	return &clientStreamClient{cc}
}

func (c *clientStreamClient) Recv(ctx context.Context, opts ...grpc.CallOption) (ClientStream_RecvClient, error) {
	stream, err := c.cc.NewStream(ctx, &ClientStream_ServiceDesc.Streams[0], "/test.ClientStream/Recv", opts...)
	if err != nil {
		return nil, err
	}
	x := &clientStreamRecvClient{stream}
	return x, nil
}

type ClientStream_RecvClient interface {
	Send(*RecvReq) error
	CloseAndRecv() (*RecvResp, error)
	grpc.ClientStream
}

type clientStreamRecvClient struct {
	grpc.ClientStream
}

func (x *clientStreamRecvClient) Send(m *RecvReq) error {
	return x.ClientStream.SendMsg(m)
}

func (x *clientStreamRecvClient) CloseAndRecv() (*RecvResp, error) {
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	m := new(RecvResp)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func (c *clientStreamClient) RecvMetadata(ctx context.Context, opts ...grpc.CallOption) (ClientStream_RecvMetadataClient, error) {
	stream, err := c.cc.NewStream(ctx, &ClientStream_ServiceDesc.Streams[1], "/test.ClientStream/RecvMetadata", opts...)
	if err != nil {
		return nil, err
	}
	x := &clientStreamRecvMetadataClient{stream}
	return x, nil
}

type ClientStream_RecvMetadataClient interface {
	Send(*RecvReq) error
	CloseAndRecv() (*RecvResp, error)
	grpc.ClientStream
}

type clientStreamRecvMetadataClient struct {
	grpc.ClientStream
}

func (x *clientStreamRecvMetadataClient) Send(m *RecvReq) error {
	return x.ClientStream.SendMsg(m)
}

func (x *clientStreamRecvMetadataClient) CloseAndRecv() (*RecvResp, error) {
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	m := new(RecvResp)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

// ClientStreamServer is the server API for ClientStream service.
// All implementations must embed UnimplementedClientStreamServer
// for forward compatibility
type ClientStreamServer interface {
	Recv(ClientStream_RecvServer) error
	RecvMetadata(ClientStream_RecvMetadataServer) error
	mustEmbedUnimplementedClientStreamServer()
}

// UnimplementedClientStreamServer must be embedded to have forward compatible implementations.
type UnimplementedClientStreamServer struct {
}

func (UnimplementedClientStreamServer) Recv(ClientStream_RecvServer) error {
	return status.Errorf(codes.Unimplemented, "method Recv not implemented")
}
func (UnimplementedClientStreamServer) RecvMetadata(ClientStream_RecvMetadataServer) error {
	return status.Errorf(codes.Unimplemented, "method RecvMetadata not implemented")
}
func (UnimplementedClientStreamServer) mustEmbedUnimplementedClientStreamServer() {}

// UnsafeClientStreamServer may be embedded to opt out of forward compatibility for this service.
// Use of this interface is not recommended, as added methods to ClientStreamServer will
// result in compilation errors.
type UnsafeClientStreamServer interface {
	mustEmbedUnimplementedClientStreamServer()
}

func RegisterClientStreamServer(s grpc.ServiceRegistrar, srv ClientStreamServer) {
	s.RegisterService(&ClientStream_ServiceDesc, srv)
}

func _ClientStream_Recv_Handler(srv interface{}, stream grpc.ServerStream) error {
	return srv.(ClientStreamServer).Recv(&clientStreamRecvServer{stream})
}

type ClientStream_RecvServer interface {
	SendAndClose(*RecvResp) error
	Recv() (*RecvReq, error)
	grpc.ServerStream
}

type clientStreamRecvServer struct {
	grpc.ServerStream
}

func (x *clientStreamRecvServer) SendAndClose(m *RecvResp) error {
	return x.ServerStream.SendMsg(m)
}

func (x *clientStreamRecvServer) Recv() (*RecvReq, error) {
	m := new(RecvReq)
	if err := x.ServerStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func _ClientStream_RecvMetadata_Handler(srv interface{}, stream grpc.ServerStream) error {
	return srv.(ClientStreamServer).RecvMetadata(&clientStreamRecvMetadataServer{stream})
}

type ClientStream_RecvMetadataServer interface {
	SendAndClose(*RecvResp) error
	Recv() (*RecvReq, error)
	grpc.ServerStream
}

type clientStreamRecvMetadataServer struct {
	grpc.ServerStream
}

func (x *clientStreamRecvMetadataServer) SendAndClose(m *RecvResp) error {
	return x.ServerStream.SendMsg(m)
}

func (x *clientStreamRecvMetadataServer) Recv() (*RecvReq, error) {
	m := new(RecvReq)
	if err := x.ServerStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

// ClientStream_ServiceDesc is the grpc.ServiceDesc for ClientStream service.
// It's only intended for direct use with grpc.RegisterService,
// and not to be introspected or modified (even as a copy)
var ClientStream_ServiceDesc = grpc.ServiceDesc{
	ServiceName: "test.ClientStream",
	HandlerType: (*ClientStreamServer)(nil),
	Methods:     []grpc.MethodDesc{},
	Streams: []grpc.StreamDesc{
		{
			StreamName:    "Recv",
			Handler:       _ClientStream_Recv_Handler,
			ClientStreams: true,
		},
		{
			StreamName:    "RecvMetadata",
			Handler:       _ClientStream_RecvMetadata_Handler,
			ClientStreams: true,
		},
	},
	Metadata: "proto/test.proto",
}

// BidirectionalStreamClient is the client API for BidirectionalStream service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://pkg.go.dev/google.golang.org/grpc/?tab=doc#ClientConn.NewStream.
type BidirectionalStreamClient interface {
	Echo(ctx context.Context, opts ...grpc.CallOption) (BidirectionalStream_EchoClient, error)
	EchoSum(ctx context.Context, opts ...grpc.CallOption) (BidirectionalStream_EchoSumClient, error)
}

type bidirectionalStreamClient struct {
	cc grpc.ClientConnInterface
}

func NewBidirectionalStreamClient(cc grpc.ClientConnInterface) BidirectionalStreamClient {
	return &bidirectionalStreamClient{cc}
}

func (c *bidirectionalStreamClient) Echo(ctx context.Context, opts ...grpc.CallOption) (BidirectionalStream_EchoClient, error) {
	stream, err := c.cc.NewStream(ctx, &BidirectionalStream_ServiceDesc.Streams[0], "/test.BidirectionalStream/Echo", opts...)
	if err != nil {
		return nil, err
	}
	x := &bidirectionalStreamEchoClient{stream}
	return x, nil
}

type BidirectionalStream_EchoClient interface {
	Send(*RecvReq) error
	Recv() (*RecvResp, error)
	grpc.ClientStream
}

type bidirectionalStreamEchoClient struct {
	grpc.ClientStream
}

func (x *bidirectionalStreamEchoClient) Send(m *RecvReq) error {
	return x.ClientStream.SendMsg(m)
}

func (x *bidirectionalStreamEchoClient) Recv() (*RecvResp, error) {
	m := new(RecvResp)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func (c *bidirectionalStreamClient) EchoSum(ctx context.Context, opts ...grpc.CallOption) (BidirectionalStream_EchoSumClient, error) {
	stream, err := c.cc.NewStream(ctx, &BidirectionalStream_ServiceDesc.Streams[1], "/test.BidirectionalStream/EchoSum", opts...)
	if err != nil {
		return nil, err
	}
	x := &bidirectionalStreamEchoSumClient{stream}
	return x, nil
}

type BidirectionalStream_EchoSumClient interface {
	Send(*RecvReq) error
	Recv() (*RecvResp, error)
	grpc.ClientStream
}

type bidirectionalStreamEchoSumClient struct {
	grpc.ClientStream
}

func (x *bidirectionalStreamEchoSumClient) Send(m *RecvReq) error {
	return x.ClientStream.SendMsg(m)
}

func (x *bidirectionalStreamEchoSumClient) Recv() (*RecvResp, error) {
	m := new(RecvResp)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

// BidirectionalStreamServer is the server API for BidirectionalStream service.
// All implementations must embed UnimplementedBidirectionalStreamServer
// for forward compatibility
type BidirectionalStreamServer interface {
	Echo(BidirectionalStream_EchoServer) error
	EchoSum(BidirectionalStream_EchoSumServer) error
	mustEmbedUnimplementedBidirectionalStreamServer()
}

// UnimplementedBidirectionalStreamServer must be embedded to have forward compatible implementations.
type UnimplementedBidirectionalStreamServer struct {
}

func (UnimplementedBidirectionalStreamServer) Echo(BidirectionalStream_EchoServer) error {
	return status.Errorf(codes.Unimplemented, "method Echo not implemented")
}
func (UnimplementedBidirectionalStreamServer) EchoSum(BidirectionalStream_EchoSumServer) error {
	return status.Errorf(codes.Unimplemented, "method EchoSum not implemented")
}
func (UnimplementedBidirectionalStreamServer) mustEmbedUnimplementedBidirectionalStreamServer() {}

// UnsafeBidirectionalStreamServer may be embedded to opt out of forward compatibility for this service.
// Use of this interface is not recommended, as added methods to BidirectionalStreamServer will
// result in compilation errors.
type UnsafeBidirectionalStreamServer interface {
	mustEmbedUnimplementedBidirectionalStreamServer()
}

func RegisterBidirectionalStreamServer(s grpc.ServiceRegistrar, srv BidirectionalStreamServer) {
	s.RegisterService(&BidirectionalStream_ServiceDesc, srv)
}

func _BidirectionalStream_Echo_Handler(srv interface{}, stream grpc.ServerStream) error {
	return srv.(BidirectionalStreamServer).Echo(&bidirectionalStreamEchoServer{stream})
}

type BidirectionalStream_EchoServer interface {
	Send(*RecvResp) error
	Recv() (*RecvReq, error)
	grpc.ServerStream
}

type bidirectionalStreamEchoServer struct {
	grpc.ServerStream
}

func (x *bidirectionalStreamEchoServer) Send(m *RecvResp) error {
	return x.ServerStream.SendMsg(m)
}

func (x *bidirectionalStreamEchoServer) Recv() (*RecvReq, error) {
	m := new(RecvReq)
	if err := x.ServerStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func _BidirectionalStream_EchoSum_Handler(srv interface{}, stream grpc.ServerStream) error {
	return srv.(BidirectionalStreamServer).EchoSum(&bidirectionalStreamEchoSumServer{stream})
}

type BidirectionalStream_EchoSumServer interface {
	Send(*RecvResp) error
	Recv() (*RecvReq, error)
	grpc.ServerStream
}

type bidirectionalStreamEchoSumServer struct {
	grpc.ServerStream
}

func (x *bidirectionalStreamEchoSumServer) Send(m *RecvResp) error {
	return x.ServerStream.SendMsg(m)
}

func (x *bidirectionalStreamEchoSumServer) Recv() (*RecvReq, error) {
	m := new(RecvReq)
	if err := x.ServerStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

// BidirectionalStream_ServiceDesc is the grpc.ServiceDesc for BidirectionalStream service.
// It's only intended for direct use with grpc.RegisterService,
// and not to be introspected or modified (even as a copy)
var BidirectionalStream_ServiceDesc = grpc.ServiceDesc{
	ServiceName: "test.BidirectionalStream",
	HandlerType: (*BidirectionalStreamServer)(nil),
	Methods:     []grpc.MethodDesc{},
	Streams: []grpc.StreamDesc{
		{
			StreamName:    "Echo",
			Handler:       _BidirectionalStream_Echo_Handler,
			ServerStreams: true,
			ClientStreams: true,
		},
		{
			StreamName:    "EchoSum",
			Handler:       _BidirectionalStream_EchoSum_Handler,
			ServerStreams: true,
			ClientStreams: true,
		},
	},
	Metadata: "proto/test.proto",
}
