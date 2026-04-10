import socket
import time


def send_cmd(cmd):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("localhost", 8083))
        s.sendall(cmd.encode())
        data = s.recv(1024)

        s.close()
        return data.decode()
    except Exception as e:
        return str(e)


print("Sending SET...")
print(send_cmd("SET name copilot"))
time.sleep(0.1)
print("Sending GET...")
print(send_cmd("GET name"))
time.sleep(0.1)
print("Sending GET unknown...")
print(send_cmd("GET unknown"))
