// taken from
// https://github.com/kubernetes/kubernetes/blob/b0b7a323cc5a4a2019b2e9520c21c7830b7f708e/pkg/kubelet/api/testing/fake_image_service.go

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
  "os"
  "fmt"
  "path/filepath"
  "strings"
  "sync"

  "github.com/golang/glog"
  "golang.org/x/net/context"
  "k8s.io/kubernetes/pkg/kubelet/api/v1alpha1/runtime"
  "k8s.io/kubernetes/pkg/kubelet/util/sliceutils"
)

type FakeImageService struct {
  sync.Mutex

  RootDir *string
  Images map[string]*runtime.Image
}

func NewFakeImageService(rootdir *string) *FakeImageService {
  return &FakeImageService{
    RootDir: rootdir,
    Images: make(map[string]*runtime.Image),
  }
}

func (s *FakeImageService) makeFakeImage(id string) *runtime.Image {
  return &runtime.Image{
    Id:       id,
    Size_:    1,
    RepoTags: []string{},
  }
}

func (s *FakeImageService) ListImages(ctx context.Context, req *runtime.ListImagesRequest) (*runtime.ListImagesResponse, error) {
  glog.Infof("ListImages %s", req.String())
  s.Lock()
  defer s.Unlock()

  filter := req.Filter;
  images := make([]*runtime.Image, 0)
  for _, img := range s.Images {
    if filter != nil && filter.Image != nil {
      if !sliceutils.StringInSlice(filter.Image.Image, img.RepoTags) {
        continue
      }
    }
    images = append(images, img)
  }
  return &runtime.ListImagesResponse {
    Images: images,
  }, nil
}


func (s *FakeImageService) ImageStatus(ctx context.Context, req *runtime.ImageStatusRequest) (*runtime.ImageStatusResponse, error) {
  glog.Infof("ImageStatus %s", req.String())
  s.Lock()
  defer s.Unlock()

  return &runtime.ImageStatusResponse {
    Image: s.Images[req.Image.Image],
  }, nil
}

func (s *FakeImageService) PullImage(ctx context.Context, req *runtime.PullImageRequest) (*runtime.PullImageResponse, error) {
  glog.Infof("PullImage %s", req.String())
  s.Lock()
  defer s.Unlock()

  image := req.Image;

  imageID := image.Image
  name := strings.SplitN(imageID, ":", 2)[0]

  if _, ok := s.Images[name]; !ok {

    path := filepath.Join(*s.RootDir, "images", name)
    if _, err := os.Stat(path); err != nil {
       return nil, fmt.Errorf("image not exists %s", path)
    }

    s.Images[name] = s.makeFakeImage(name)
  }

  return &runtime.PullImageResponse {
    ImageRef: name,
  }, nil
}

func (s *FakeImageService) RemoveImage(ctx context.Context, req *runtime.RemoveImageRequest) (*runtime.RemoveImageResponse, error) {
  glog.Infof("RemoveImage %s", req.String())
  s.Lock()
  defer s.Unlock()
  image := req.Image
  delete(s.Images, image.Image)
  return &runtime.RemoveImageResponse {
  }, nil
}
