from flask import Flask, render_template, request, jsonify
from flask_socketio import SocketIO, emit
import socket
import json
from langchain.chat_models import ChatOpenAI
from langchain.schema import SystemMessage, HumanMessage
import re

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

# LangChain 設定
llm = ChatOpenAI(
    openai_api_key='KEY',
    model='gpt-4o'  # 改成你有權限的模型
)

clients = []

@app.route('/')
def index():
    return render_template("index.html")

@socketio.on('orientation')
def handle_orientation(data):
    msg = f"{data['beta']:.2f},{data['gamma']:.2f}\n"
    broadcast_to_clients(msg)

@socketio.on('led')
def handle_led(data):
    msg = "LED ON\n" if data['command'] == 'ON' else "LED OFF\n"
    broadcast_to_clients(msg)

@socketio.on('stop')
def handle_stop():
    broadcast_to_clients("STOP\n")

@app.route('/gpt_action', methods=['POST'])
def gpt_action():
    data = request.get_json()
    prompt = data.get('prompt', '').strip()

    full_prompt = (
        f"根據以下指令產生一組車車控制動作，每個動作格式為："
        f"{{\"beta\":數字, \"gamma\":數字, \"duration\":毫秒}}，"
        f"只輸出 JSON 陣列，不要加上說明文字。"
        f"指令是: {prompt}"
    )

    try:
        response = llm.invoke([
            SystemMessage(content=(
                "你是一個控制車車的專家，你的任務是根據指令，只回應 JSON 陣列，"
                "不要加上任何其他說明，也不要換行，只回傳純 JSON，例如："
                "[{\"beta\":30, \"gamma\":0, \"duration\":1200}]"
            )),
            HumanMessage(content=full_prompt)
        ])
        text = response.content
        print(f"收到的 GPT 回應:\n{text}")

        clean_text = text.strip()

        if clean_text.startswith("```"):
            clean_text = re.sub(r"^```.*?\n", "", clean_text)
            clean_text = clean_text.rstrip("`").rstrip()

        commands = json.loads(clean_text)

        for i, cmd in enumerate(commands):
            socketio.start_background_task(send_command_with_delay, cmd, i)

        return jsonify({"status": "ok", "commands": commands})

    except Exception as e:
        return jsonify({"error": str(e)}), 500

def send_command_with_delay(cmd, index):
    if 'duration' in cmd:
        msg = f"{cmd['beta']},{cmd['gamma']},{cmd['duration']}\n"
    else:
        msg = f"{cmd['beta']},{cmd['gamma']}\n"

    broadcast_to_clients(msg)
    print(f"✅ 已送出: {msg.strip()}")

def broadcast_to_clients(msg):
    for c in clients[:]:
        try:
            c.sendall(msg.encode())
        except:
            clients.remove(c)

def tcp_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('', 5001))
    s.listen(5)
    print("等待ESP32連接中...")
    while True:
        conn, addr = s.accept()
        print(f"ESP32連接：{addr}")
        clients.append(conn)

        threading.Thread(target=receive_data_from_client, args=(conn,), daemon=True).start()

def receive_data_from_client(conn):
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                break
            msg = data.decode().strip()
            print(f"收到ESP32資料: {msg}")

            if msg.startswith("DIST:"):
                try:
                    distance = float(msg.replace("DIST:", "").strip())

                    socketio.emit('distance_update', {'distance': distance})
                except ValueError:
                    print("收到錯誤的距離資料")
    except Exception as e:
        print(f"接收資料時出錯: {e}")
    finally:
        conn.close()
        if conn in clients:
            clients.remove(conn)
        print("ESP32 斷線")



if __name__ == '__main__':
    import threading
    threading.Thread(target=tcp_server, daemon=True).start()
    socketio.run(app, host='0.0.0.0', port=5000)
