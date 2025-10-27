# ⚡ raw — A Minimal Raw TCP Echo Server & Client

> A tiny end-to-end playground for socket programming.  
> **Server:** C (pure sockets)  
> **Client:** Rust 🦀  
> **Goal:** Keep it raw. Keep it fast. Echo back ≤ 20 chars.

---

## 🚀 Quickstart

### 🧱 Build & Run Locally

#### 1️⃣ Start the Server
```bash
$ make -C server

$ ./server/bin/raw_server 0.0.0.0 9000

Logs

🔧 Building raw_server ...
✅ Build complete! Binary: ./server/bin/raw_server
⚡ raw TCP server listening on 0.0.0.0:9000 (max 20 chars per message)

2️⃣ Run the Client

$ cd client
$ cargo run -- "hello world" 127.0.0.1:9000

Output

🚀 Connecting to 127.0.0.1:9000 ...
📤 Sending message: hello world
📥 Received echo: hello world
✅ Done!

🧩 How It Works
Component	Language	Description
🖥️ Server	C	Handles raw TCP connections, validates message length (≤ 20 chars), and echoes back the payload.
💬 Client	Rust	Connects, sends message, and prints whatever the server sends back.
🐳 Docker	Compose	Spins up both client and server containers in one command.
☁️ Cloud	Kubernetes	Portable manifests for AWS EKS, Azure AKS, or GCP GKE.
🐳 Run with Docker

$ docker compose up --build

Logs

🛠️ Building server ...
🦀 Building client ...
🌐 Starting containers ...
🟢 raw-server_1  | raw TCP server listening on 0.0.0.0:9000
🟣 raw-client_1  | Connecting to server:9000 ...
🟣 raw-client_1  | Received echo: hello

Stop everything:

$ docker compose down

☁️ Deploy Anywhere (AWS / Azure / GCP)

kubectl apply -f k8s/deployment.yaml
kubectl apply -f k8s/service.yaml

Then check:

$ kubectl get svc raw-server
NAME         TYPE           CLUSTER-IP      EXTERNAL-IP      PORT(S)          AGE
raw-server   LoadBalancer   10.0.23.145     35.212.88.14     9000:32212/TCP   20s

Connect via netcat

$ nc 35.212.88.14 9000
> hello cloud ☁️
< hello cloud ☁️

🧠 Tech Summary

    🧩 C (server): low-level sockets, manual I/O, recv()/send()

    🦀 Rust (client): modern, safe concurrency & networking

    🐳 Docker: portable build/runtime for both

    ☁️ Kubernetes: universal deployment manifests

💬 Example Demo Log

👋  Client connected from 172.17.0.2:50432
💡  Received: "system design ftw"
🔁  Echoed:   "system design ftw"
⚡  Connection closed

🎯 Project Goals

    ✅ Learn raw socket programming (C & Rust)

    ✅ Observe TCP behavior in containers and K8s

    ✅ Build portable, cloud-agnostic infra

    ✅ Keep the code minimal and elegant

🧰 Future Ideas

    🔒 Add TLS support

    📈 Add metrics (Prometheus + Grafana)

    🧠 Experiment with async Rust server

    🧪 Add fuzz tests