package main;

import (
  "flag"
  "fmt"
  "os"
  "syscall"
  "net"

  "google.golang.org/grpc"
  "k8s.io/kubernetes/pkg/kubelet/api/v1alpha1/runtime"
  "fakecr/service"
)

var (
  listen  = flag.String("listen", "/run/fake.sock", "The sockets to listen on, e.g. /run/fake.sock")

  node = flag.String("node", "node", "node name")

  bindir = flag.String("bindir", "/root/bin", "bindir")

  rootdir = flag.String("rootdir", "/root", "rootdir")
)

func run(addr string) error {
  server := grpc.NewServer()

  runtime.RegisterImageServiceServer(server, service.NewFakeImageService(rootdir))
  runtime.RegisterRuntimeServiceServer(server, service.NewFakeRuntimeService(node, rootdir, bindir))

  if err := syscall.Unlink(addr); err != nil && !os.IsNotExist(err) {
    return err
  }

  socket, err := net.Listen("unix", addr)
  if err != nil {
    return err
  }

  defer socket.Close()
  return server.Serve(socket)
}

func main() {
  flag.Parse()

  err := run(*listen)
  if err != nil {
    fmt.Println("Initialize fake server failed: ", err)
    os.Exit(1)
  }
}
