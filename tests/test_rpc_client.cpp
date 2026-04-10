#include "kvrpc/rpc_client.h"
#include <cassert>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>

using namespace kvrpc;

// 一个极简的 TCP 阻塞 Echo Server，仅用于测试 RpcClient 是否能打通网络
void run_mock_server() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(9998);
  bind(server_fd, (struct sockaddr *)&address, sizeof(address));
  listen(server_fd, 3);

  std::cout << "[Mock Server] Started listening on port 9998..." << std::endl;

  int client_fd = accept(server_fd, nullptr, nullptr);
  std::cout << "[Mock Server] Accepted connection!" << std::endl;

  // 读取请求长度
  uint32_t len = 0;
  recv(client_fd, &len, sizeof(len), MSG_WAITALL);
  std::vector<char> buf(len);
  recv(client_fd, buf.data(), len, MSG_WAITALL);

  // 解析请求
  Serializer req_sz(buf);
  std::string method, payload_str;
  req_sz.Deserialize(method, payload_str);

  std::cout << "[Mock Server] Received RPC call: " << method
            << " with args: " << payload_str << std::endl;

  // 模拟构造回包：加上 Response 字样
  Serializer resp_sz;
  resp_sz.Serialize(std::string("Response: ") + payload_str);

  uint32_t resp_len = resp_sz.GetBuffer().size();
  send(client_fd, &resp_len, sizeof(resp_len), 0);
  send(client_fd, resp_sz.GetBuffer().data(), resp_len, 0);

  close(client_fd);
  close(server_fd);
}

int main() {
  // 启动后台测试服务器
  std::thread server_thread(run_mock_server);
  std::this_thread::sleep_for(
      std::chrono::milliseconds(100)); // 保证服务器先启动

  // 初始化客户端与连接池
  auto pool = std::make_shared<ConnectionPool>("127.0.0.1", 9998, 2);
  RpcClient client(pool);

  // 发起异步 RPC 请求 (在后台线程连网、打包、发包)
  std::cout << "[Client] Calling ECHO rpc via std::future..." << std::endl;
  std::future<std::string> fut =
      client.Call<std::string>("ECHO", std::string("hello async rpc"));

  // 主线程此时可以做别的事...
  std::cout << "[Client] Doing other works while waiting for I/O..."
            << std::endl;

  // 阻塞等待网络结果完毕，并反序列化完成
  std::string result = fut.get();

  std::cout << "[Client] Got result: " << result << std::endl;
  assert(result == "Response: hello async rpc");

  server_thread.join();
  std::cout << "All RPC Client & Async Futures tests passed!" << std::endl;

  return 0;
}
