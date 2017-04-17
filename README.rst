===================
Rootless Kubernetes
===================

This is still a work in progress. A lot of changes are going to be
made. Application developers should go with Minikube__ .

.. __: https://github.com/kubernetes/minikube

It is impossible to run Kubernetes without root privilege. As of
Kubernetes 1.6.1, unless a few kernel parameters set to its desired
value, kubelet won't start any container. Run :code:`./sysctl
configure` with root privilege to set these kernel parameters, and
:code:`./sysctl restore` to restore them later. And Arch Linux users
should read no further, since `user namespace is disabled in Arch
Linux`__. To create other namespaces, a non-root user has to first
create a user namespace.

.. __: https://wiki.archlinux.org/index.php/Linux_Containers#Privileged_containers_or_unprivileged_containers

Run :code:`./setup` to make an Alpine Linux chroot, and download
prebuilt etcd and kubernetes binaries. You may export environment
variables in :code:`./vars` to change to a different mirror. You may
also run :code:`./test` to check if following examples really work.

================= =========================== =======================================
\                 Environment variable        Default
================= =========================== =======================================
`Alpine Linux`__  :code:`ALPINE_MIRROR`       http://dl-cdn.alpinelinux.org/alpine
`etcd`__          :code:`ETCD_MIRROR`         https://storage.googleapis.com/etcd
`Kuberenetes`__   :code:`KUBERNETES_MIRROR`   https://dl.k8s.io
================= =========================== =======================================

.. __: https://alpinelinux.org/
.. __: https://github.com/coreos/etcd/releases/
.. __: https://github.com/kubernetes/kubernetes/blob/master/CHANGELOG.md

Once done, run :code:`./enter-chroot` to start a shell in the
chroot.


Standalone kubelet
==================

Examples in this section are taken from `What even is a kubelet?`__

.. __: http://kamalmarhubi.com/blog/2015/08/27/what-even-is-a-kubelet/

start fake CRI runtime and kubelet

.. code::

    # cd
    # ./bin/start-standalone

In standalone mode, kubelet polls a directory instead of API server
for pod manifests.

.. code::

    # cp manifests/pod.yaml nodes/node1/kubelet/manifests

here is the :code:`manifests/pod.yaml`

.. code::

    apiVersion: v1
    kind: Pod
    metadata:
      name: hello
    spec:
      containers:
      - name: hello
        image: hello

Instead of pulling images from docker registry, fake CRI runtime looks
up scripts under :code:`images` directory.

here is the :code:`images/hello`

.. code::

    ncat -k -c 'echo hello' -l 0.0.0.0 80


The network namespace of each pod is created with
:code:`ip netns add ${hostname}`, and run :code:`/root/bin/showip
${hostname}` will show its ip address.

.. code::

    # ncat --recv-only $(./bin/showip hello-node1) 80
    hello



Binding
=======

Examples in this section are taken from `Kubernetes from the ground
up: the API server`__ and `Kubernetes from the ground up: the
scheduler`__

.. __: http://kamalmarhubi.com/blog/2015/09/06/kubernetes-from-the-ground-up-the-api-server/
.. __: http://kamalmarhubi.com/blog/2015/11/17/kubernetes-from-the-ground-up-the-scheduler/

We begins with single master first. etcd and API server runs with the
same network namespace as dnsmasq.

.. code::

    # cd
    # ./bin/start-single-master

start a node, :code:`node1`

.. code::

    # ./bin/newnode node1
    # kubectl get nodes
    NAME      STATUS    AGE       VERSION
    node1     Ready     1s        v1.6.1

create a pod

.. code::

    # kubectl create --filename manifests/pod.yaml
    pod "hello" created
    # kubectl get pods
    NAME      READY     STATUS    RESTARTS   AGE
    hello     0/1       Pending   0          4s

As you can see, the pod is pending. We can bind it manually to a node
to make it run.

.. code::

    # kubectl create --filename manifests/bind.yaml
    binding "hello" created
    # kubectl get pods --watch
    NAME      READY     STATUS              RESTARTS   AGE
    hello     0/1       ContainerCreating   0          10s
    hello     1/1       Running   0         14s
    ^C
    # ncat --recv-only $(kubectl get pod hello -o jsonpath='{ .status.podIP }') 80
    hello

And here is the :code:`manifests/bind.yaml`

.. code::

    apiVersion: v1
    kind: Binding
    metadata:
      name: hello
    target:
      apiVersion: v1
      kind: Node
      name: node1

We can also run a scheduler to do the binding.

.. code::

    # cd
    # ./bin/start-single-master scheduler
    # ./bin/newnode node1
    # kubectl get nodes
    NAME      STATUS    AGE       VERSION
    node1     Ready     1s        v1.6.1
    # kubectl create --filename manifests/pod.yaml
    pod "hello" created
    # kubectl get pods
    NAME      READY     STATUS    RESTARTS   AGE
    hello     1/1       Running   0          4s
    # ncat --recv-only $(kubectl get pod hello -o jsonpath='{ .status.podIP }') 80
    hello


Replicas and Rolling update
===========================

start etcd, API server, scheduler and controller-manager

.. code::

    # cd
    # ./bin/start-single-master scheduler controller-manager
    # ./bin/newnode node1 node2 node3
    # kubectl get nodes
    NAME      STATUS    AGE       VERSION
    node1     Ready     6s        v1.6.1
    node2     Ready     6s        v1.6.1
    node3     Ready     5s        v1.6.1

create replicaset, which in turn will create pods

.. code::

    # kubectl create --filename manifests/rs.yaml
    replicaset "hello" create
    # kubectl get rs
    NAME      DESIRED   CURRENT   READY     AGE
    hello     3         3         0         5s
    # kubectl get pods
    NAME          READY     STATUS    RESTARTS   AGE
    hello-1s8jr   1/1       Running   0          10s
    hello-hsz96   1/1       Running   0          10s
    hello-j6r04   1/1       Running   0          10s

node of each pod

.. code::

    # kubectl get pods -o custom-columns='Name:.metadata.name,Node:.spec.nodeName'
    Name          Node
    hello-1s8jr   node1
    hello-hsz96   node2
    hello-j6r04   node3

check if all respond with hello

.. code::

    # kubectl get pods -lapp=hello -o jsonpath='{range .items[*] }{ .status.podIP }{"\n"}{ end }' | xargs -I {} ncat --recv-only {} 80
    hello
    hello
    hello

scale up to 4 replicas

.. code::

    # kubectl scale --replicas=4 --filename manifests/rs.yaml
    replicaset "hello" scaled
    # kubectl get rs
    NAME      DESIRED   CURRENT   READY     AGE
    hello     4         4         4         20s


deployment will create new replicaset on rolling update, decrease the
number of replicas of the old replicaset and increase the number of
replicas of the new replicaset

.. code::

    # cd
    # ./bin/start-single-master scheduler controller-manager
    # ./bin/newnode node1 node2 node3
    # kubectl get nodes
    NAME      STATUS    AGE       VERSION
    node1     Ready     6s        v1.6.1
    node2     Ready     6s        v1.6.1
    node3     Ready     5s        v1.6.1

create a new deployment

.. code::

    # kubectl create --filename manifests/deployment.yaml
    deployment "hello" created
    # kubectl get deployment
    NAME      DESIRED   CURRENT   UP-TO-DATE   AVAILABLE   AGE
    hello     3         3         3            0           5s
    # kubectl get rs
    NAME               DESIRED   CURRENT   READY     AGE
    hello-4050249537   3         3         3         17s
    # kubectl get pods -lapp=hello -o jsonpath='{range .items[*] }{ .status.podIP }{"\n"}{ end }' | xargs -I {} ncat --recv-only {} 80
    hello
    hello
    hello

change image to hello-world to start a rolling update

.. code::

    # kubectl set image deployment/hello hello=hello-world
    deployment "hello" image updated
    # kubectl rollout status deployment/hello
    Waiting for rollout to finish: 1 out of 3 new replicas have been updated...
    Waiting for rollout to finish: 1 out of 3 new replicas have been updated...
    Waiting for rollout to finish: 1 out of 3 new replicas have been updated...
    Waiting for rollout to finish: 2 out of 3 new replicas have been updated...
    Waiting for rollout to finish: 2 out of 3 new replicas have been updated...
    Waiting for rollout to finish: 2 out of 3 new replicas have been updated...
    Waiting for rollout to finish: 1 old replicas are pending termination...
    Waiting for rollout to finish: 1 old replicas are pending termination...
    deployment "hello" successfully rolled out
    # kubectl get rs
    NAME               DESIRED   CURRENT   READY     AGE
    hello-1359538582   3         3         3         22s
    hello-4050249537   0         0         0         56s
    # kubectl get pods -lapp=hello -o jsonpath='{range .items[*] }{ .status.podIP }{"\n"}{ end }' | xargs -I {} ncat --recv-only {} 80
    hello world
    hello world
    hello world
