// taken from
// https://github.com/kubernetes/kubernetes/blob/b0b7a323cc5a4a2019b2e9520c21c7830b7f708e/pkg/kubelet/api/testing/fake_runtime_service.go

/*
Copyright 2016 The Kubernetes Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/


package service

import (
  "fmt"
  "os"
  "os/exec"
  "path/filepath"
  "time"
  "sync"

  "github.com/golang/glog"
  "golang.org/x/net/context"
  "k8s.io/kubernetes/pkg/kubelet/api/v1alpha1/runtime"
)

var (
  version = "0.0.0"
  FakeRuntimeName  = "fake"
)

type FakePodSandbox struct {
  runtime.PodSandboxStatus
  Hostname string
}

type FakeContainer struct {
  runtime.ContainerStatus
  SandboxID string
}

type FakeRuntimeService struct {
  sync.Mutex

  FakeStatus *runtime.RuntimeStatus
  Containers map[string]*FakeContainer
  Sandboxes  map[string]*FakePodSandbox

  Node *string
  RootDir *string
  BinDir *string
}

func NewFakeRuntimeService(node *string, rootdir *string, bindir *string) *FakeRuntimeService {
  return &FakeRuntimeService{
    Containers: make(map[string]*FakeContainer),
    Sandboxes:  make(map[string]*FakePodSandbox),
    Node: node,
    RootDir: rootdir,
    BinDir: bindir,
  }
}

func Run(name string, arg ...string) error {
  cmd := exec.Command(name, arg ...)
  cmd.Stdin = os.Stdin
  cmd.Stdout = os.Stdout
  cmd.Stderr = os.Stderr

  return cmd.Run()
}

func Output(name string, arg ...string) ([]byte, error) {
  cmd := exec.Command(name, arg ...)
  cmd.Stdin = os.Stdin
  cmd.Stderr = os.Stderr

  return cmd.Output()
}


func (s *FakeRuntimeService) Version(ctx context.Context, req *runtime.VersionRequest) (*runtime.VersionResponse, error) {
  glog.Infof("Version %s", req.String())
  return &runtime.VersionResponse{
    Version:           req.Version,
    RuntimeName:       FakeRuntimeName,
    RuntimeVersion:    version,
    RuntimeApiVersion: version,
  }, nil
}

func (s *FakeRuntimeService) Status(ctx context.Context, req *runtime.StatusRequest) (*runtime.StatusResponse, error) {
  glog.Infof("Status %s", req.String())
  return &runtime.StatusResponse {
    Status: &runtime.RuntimeStatus {
      Conditions: []*runtime.RuntimeCondition{
        &runtime.RuntimeCondition{
          Type:   runtime.RuntimeReady,
          Status: true,
        },
        &runtime.RuntimeCondition{
          Type:   runtime.NetworkReady,
          Status: true,
        },
      },
    },
  }, nil
}

func (s *FakeRuntimeService) RunPodSandbox(ctx context.Context, req *runtime.RunPodSandboxRequest) (*runtime.RunPodSandboxResponse, error) {
  glog.Infof("RunPodSandbox %s", req.String())
  s.Lock()
  defer s.Unlock()

  config := req.Config
  podSandboxID := BuildSandboxName(config.Metadata)
  createdAt := time.Now().Unix()
  readyState := runtime.PodSandboxState_SANDBOX_READY

  if err := Run(filepath.Join(*s.BinDir, "pod"), "create", *s.Node, podSandboxID, config.Hostname); err != nil {
    return nil, err
  }

  if output, err := Output(filepath.Join(*s.BinDir, "showip"), config.Hostname); err != nil {
    return nil, err
  } else {
    ip := string(output[:])

    s.Sandboxes[podSandboxID] = &FakePodSandbox {
      PodSandboxStatus: runtime.PodSandboxStatus {
        Id:        podSandboxID,
        Metadata:  config.Metadata,
        State:     readyState,
        CreatedAt: createdAt,
        Network: &runtime.PodSandboxNetworkStatus{
          Ip: ip,
        },
        Labels:      config.Labels,
        Annotations: config.Annotations,
      },
      Hostname: config.Hostname,
    }

    return &runtime.RunPodSandboxResponse{
      PodSandboxId: podSandboxID,
    }, nil
  }
}

func (s *FakeRuntimeService) StopPodSandbox(ctx context.Context, req *runtime.StopPodSandboxRequest) (*runtime.StopPodSandboxResponse, error) {
  glog.Infof("StopPodSandbox %s", req.String())
  s.Lock()
  defer s.Unlock()

  podSandboxID := req.PodSandboxId
  notReadyState := runtime.PodSandboxState_SANDBOX_NOTREADY
  if sb, ok := s.Sandboxes[podSandboxID]; ok {
    sb.State = notReadyState
  } else {
    return nil, fmt.Errorf("pod sandbox %s not found", podSandboxID)
  }

  return &runtime.StopPodSandboxResponse {
  }, nil
}

func (s *FakeRuntimeService) RemovePodSandbox(ctx context.Context, req *runtime.RemovePodSandboxRequest) (*runtime.RemovePodSandboxResponse, error) {
  glog.Infof("RemovePodSandbox %s", req.String())
  s.Lock()
  defer s.Unlock()
  podSandboxID := req.PodSandboxId

  if sb, ok := s.Sandboxes[podSandboxID]; ok {
    Run(filepath.Join(*s.BinDir, "pod"), "remove", *s.Node, podSandboxID, sb.Hostname)
  } else {
    return nil, fmt.Errorf("pod sandbox %s not found", podSandboxID)
  }

  delete(s.Sandboxes, podSandboxID)
  return &runtime.RemovePodSandboxResponse {
  }, nil
}

func (s *FakeRuntimeService) PodSandboxStatus(ctx context.Context, req *runtime.PodSandboxStatusRequest) (*runtime.PodSandboxStatusResponse, error) {
  glog.Infof("PodSandboxStatus %s", req.String())
  s.Lock()
  defer s.Unlock()
  podSandboxID := req.PodSandboxId
  sb, ok := s.Sandboxes[podSandboxID]
  if !ok {
    return nil, fmt.Errorf("pod sandbox %q not found", podSandboxID)
  }

  return &runtime.PodSandboxStatusResponse {
    Status: &sb.PodSandboxStatus,
  }, nil
}

func (s *FakeRuntimeService) ListPodSandbox(ctx context.Context, req *runtime.ListPodSandboxRequest) (*runtime.ListPodSandboxResponse, error) {
  glog.Infof("ListPodSandbox %s", req.String())
  s.Lock()
  defer s.Unlock()

  filter := req.Filter
  result := make([]*runtime.PodSandbox, 0)
  for id, sb := range s.Sandboxes {
    if filter != nil {
      if filter.Id != "" && filter.Id != id {
        continue
      }
      if filter.State != nil && filter.GetState().State != sb.State {
        continue
      }
      if filter.LabelSelector != nil && !filterInLabels(filter.LabelSelector, sb.GetLabels()) {
        continue
      }
    }

    result = append(result, &runtime.PodSandbox{
      Id:          sb.Id,
      Metadata:    sb.Metadata,
      State:       sb.State,
      CreatedAt:   sb.CreatedAt,
      Labels:      sb.Labels,
      Annotations: sb.Annotations,
    })
  }

  return &runtime.ListPodSandboxResponse {
    Items: result,
  }, nil
}

func (s *FakeRuntimeService) PortForward(ctx context.Context, req *runtime.PortForwardRequest) (*runtime.PortForwardResponse, error) {
  glog.Infof("PortForward %s", req.String())
  s.Lock()
  defer s.Unlock()

  return &runtime.PortForwardResponse{}, nil
}


func WriteInit(path string, envs []*runtime.KeyValue, mounts []*runtime.Mount) error {
  if file, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE, 0700); err != nil {
    return err
  } else {
     defer file.Close()

     if _, err := fmt.Fprintf(file, "#!/usr/bin/env bash\n\n"); err != nil {
       return err
     }

     for _, e := range envs {
       if _, err := fmt.Fprintf(file, "export %q=%q\n", e.Key, e.Value); err != nil {
         return err
       }
     }

     for _, m := range mounts {
       if _, err := fmt.Fprintf(file, "mount -o bind %q %q\n", m.HostPath, m.ContainerPath); err != nil {
         return err
       }
     }

    return nil
  }
}


func (s *FakeRuntimeService) CreateContainer(ctx context.Context, req *runtime.CreateContainerRequest) (*runtime.CreateContainerResponse, error) {
  glog.Infof("CreateContainer %s", req.String())
  s.Lock()
  defer s.Unlock()

  config := req.Config
  podSandboxID := req.PodSandboxId

  containerID := BuildContainerName(config.Metadata, podSandboxID)
  createdAt := time.Now().Unix()
  createdState := runtime.ContainerState_CONTAINER_CREATED
  imageRef := config.Image.Image

  path := filepath.Join(*s.RootDir, "nodes", *s.Node, "pods", podSandboxID, containerID + ".sh")
  if err := WriteInit(path, config.Envs, config.Mounts); err != nil {
    return nil, err
  }

  s.Containers[containerID] = &FakeContainer{
    ContainerStatus: runtime.ContainerStatus {
      Id:          containerID,
      Metadata:    config.Metadata,
      Image:       config.Image,
      ImageRef:    imageRef,
      CreatedAt:   createdAt,
      State:       createdState,
      Labels:      config.Labels,
      Annotations: config.Annotations,
    },
    SandboxID: podSandboxID,
  }

  return &runtime.CreateContainerResponse {
    ContainerId: containerID,
  }, nil
}

func (s *FakeRuntimeService) StartContainer(ctx context.Context, req *runtime.StartContainerRequest) (*runtime.StartContainerResponse, error) {
  glog.Infof("StartContainer %s", req.String())
  s.Lock()
  defer s.Unlock()

  containerID := req.ContainerId
  c, ok := s.Containers[containerID]
  if !ok {
    return nil, fmt.Errorf("container %s not found", containerID)
  }

  podSandboxID := c.SandboxID
  sb, ok := s.Sandboxes[podSandboxID]
  if !ok {
    return nil, fmt.Errorf("podsandbox %s not found", podSandboxID)
  }

  startedAt := time.Now().Unix()
  runningState := runtime.ContainerState_CONTAINER_RUNNING
  c.State = runningState
  c.StartedAt = startedAt

  if err := Run(filepath.Join(*s.BinDir, "ct"), "start", *s.Node, podSandboxID, sb.Hostname, containerID, c.ImageRef); err != nil {
    return nil, err
  }

  return &runtime.StartContainerResponse {
  }, nil
}

func (s *FakeRuntimeService) StopContainer(ctx context.Context, req *runtime.StopContainerRequest) (*runtime.StopContainerResponse, error) {
  glog.Infof("StopContainer %s", req.String())
  s.Lock()
  defer s.Unlock()

  containerID := req.ContainerId
  c, ok := s.Containers[containerID]
  if !ok {
    return nil, fmt.Errorf("container %q not found", containerID)
  }

  // Set container to exited state.
  finishedAt := time.Now().Unix()
  exitedState := runtime.ContainerState_CONTAINER_EXITED
  c.State = exitedState
  c.FinishedAt = finishedAt

  if err := Run(filepath.Join(*s.BinDir, "ct"), "stop", *s.Node, c.SandboxID, containerID); err != nil {
    return nil, err
  }

  return &runtime.StopContainerResponse {
  }, nil
}

func (s *FakeRuntimeService) RemoveContainer(ctx context.Context, req *runtime.RemoveContainerRequest) (*runtime.RemoveContainerResponse, error) {
  glog.Infof("RemoveContainer %s", req.String())
  s.Lock()
  defer s.Unlock()
  containerID := req.ContainerId
  delete(s.Containers, containerID)
  return &runtime.RemoveContainerResponse {
  }, nil
}

func (s *FakeRuntimeService) CheckState(c *FakeContainer) {
  if c.State != runtime.ContainerState_CONTAINER_RUNNING {
    return
  }

  if err := Run(filepath.Join(*s.BinDir, "ct"), "check", *s.Node, c.SandboxID, c.Id); err != nil {
    c.State = runtime.ContainerState_CONTAINER_EXITED
  }
}

func (s *FakeRuntimeService) ListContainers(ctx context.Context, req *runtime.ListContainersRequest) (*runtime.ListContainersResponse, error) {
  glog.Infof("ListContainers %s", req.String())
  s.Lock()
  defer s.Unlock()

  filter := req.Filter;
  result := make([]*runtime.Container, 0)
  for _, c := range s.Containers {
    s.CheckState(c)

    if filter != nil {
      if filter.Id != "" && filter.Id != c.Id {
        continue
      }
      if filter.PodSandboxId != "" && filter.PodSandboxId != c.SandboxID {
        continue
      }
      if filter.State != nil && filter.GetState().State != c.State {
        continue
      }
      if filter.LabelSelector != nil && !filterInLabels(filter.LabelSelector, c.GetLabels()) {
        continue
      }
    }

    result = append(result, &runtime.Container{
      Id:           c.Id,
      CreatedAt:    c.CreatedAt,
      PodSandboxId: c.SandboxID,
      Metadata:     c.Metadata,
      State:        c.State,
      Image:        c.Image,
      ImageRef:     c.ImageRef,
      Labels:       c.Labels,
      Annotations:  c.Annotations,
    })
  }

  return &runtime.ListContainersResponse {
    Containers: result,
  }, nil
}

func (s *FakeRuntimeService) ContainerStatus(ctx context.Context, req *runtime.ContainerStatusRequest) (*runtime.ContainerStatusResponse, error) {
  glog.Infof("ContainerStatus %s", req.String())
  s.Lock()
  defer s.Unlock()

  containerID := req.ContainerId

  c, ok := s.Containers[containerID]
  if !ok {
    return nil, fmt.Errorf("container %q not found", containerID)
  }

  s.CheckState(c)

  return &runtime.ContainerStatusResponse {
    Status: &c.ContainerStatus,
  }, nil
}

func (s *FakeRuntimeService) ExecSync(ctx context.Context, req *runtime.ExecSyncRequest) (*runtime.ExecSyncResponse, error) {
  glog.Infof("ExecSync %s", req.String())
  s.Lock()
  defer s.Unlock()
  return &runtime.ExecSyncResponse {
    Stdout: nil,
    Stderr: nil,
    ExitCode: -1,
  }, nil
}

func (s *FakeRuntimeService) Exec(ctx context.Context, req *runtime.ExecRequest) (*runtime.ExecResponse, error) {
  glog.Infof("Exec %s", req.String())
  s.Lock()
  defer s.Unlock()
  return &runtime.ExecResponse{}, nil
}

func (s *FakeRuntimeService) Attach(ctx context.Context, req *runtime.AttachRequest) (*runtime.AttachResponse, error) {
  glog.Infof("Attach %s", req.String())
  s.Lock()
  defer s.Unlock()

  return &runtime.AttachResponse{}, nil
}

func (s *FakeRuntimeService) UpdateRuntimeConfig(ctx context.Context, req *runtime.UpdateRuntimeConfigRequest) (*runtime.UpdateRuntimeConfigResponse, error) {
  glog.Infof("UpdateRuntimeConfig %s", req.String())
  return &runtime.UpdateRuntimeConfigResponse {
  }, nil
}
