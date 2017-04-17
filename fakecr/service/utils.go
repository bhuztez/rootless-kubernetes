// taken from
// https://github.com/kubernetes/kubernetes/blob/b0b7a323cc5a4a2019b2e9520c21c7830b7f708e/pkg/kubelet/api/testing/utils.go

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
  "k8s.io/kubernetes/pkg/kubelet/api/v1alpha1/runtime"
)

func BuildContainerName(metadata *runtime.ContainerMetadata, sandboxID string) string {
  return fmt.Sprintf("%s_%s_%d", sandboxID, metadata.Name, metadata.Attempt)
}

func BuildSandboxName(metadata *runtime.PodSandboxMetadata) string {
  return fmt.Sprintf("%s_%s_%s_%d", metadata.Name, metadata.Namespace, metadata.Uid, metadata.Attempt)
}

func filterInLabels(filter, labels map[string]string) bool {
  for k, v := range filter {
    if value, ok := labels[k]; ok {
      if value != v {
        return false
      }
    } else {
      return false
    }
  }

  return true
}

