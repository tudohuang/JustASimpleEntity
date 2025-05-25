#include <WiFi.h>
#include <Preferences.h>

Preferences prefs;
WiFiClient client;
const uint16_t server_port = 5001;
const char* default_server_ip = "192.168.50.42";
String server_ip;

#define IN1 32
#define IN2 33
#define ENA 25
#define IN3 26
#define IN4 27
#define ENB 14
#define TRIG_PIN 5
#define ECHO_PIN 18
#define BUZZER 4

String selectWiFi();
void handleMovement(float beta, float gamma, int duration);
void backRadar();
void stopMotors();
void moveForward();
void moveBackward();
void turnLeft();
void turnRight();

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENB, OUTPUT);
  digitalWrite(ENA, HIGH);
  digitalWrite(ENB, HIGH);

  prefs.begin("wifi", false);
  String ssid = prefs.getString("ssid", "");
  String pwd = prefs.getString("pwd", "");
  server_ip = prefs.getString("server_ip", default_server_ip);
  prefs.end();

  if (ssid == "" || pwd == "") {
    ssid = selectWiFi();
  } else {
    WiFi.begin(ssid.c_str(), pwd.c_str());
    Serial.println("\n嘗試連線 WiFi: " + ssid);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi 連線成功");

  if (!client.connect(server_ip.c_str(), server_port)) {
    Serial.println("❌ 無法連到伺服器");
    while (1);
  }
  Serial.println("✅ 成功連線至伺服器");
}

void loop() {
  backRadar();

  if (client.available()) {
    String msg = client.readStringUntil('\n');
    msg.trim();
    Serial.println("收到訊息：" + msg);

    float beta, gamma;
    int duration = -1;
    int parsed = sscanf(msg.c_str(), "%f,%f,%d", &beta, &gamma, &duration);

    if (parsed >= 2) {
      Serial.printf("解析 beta: %.2f gamma: %.2f", beta, gamma);
      if (parsed == 3) Serial.printf(" duration: %d\n", duration);
      else Serial.println(" （無 duration）");

      handleMovement(beta, gamma, duration);
    } else {
      Serial.println("指令格式錯誤");
    }
  }
}

String selectWiFi() {
  Serial.println("🔍 掃描 WiFi 中...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("無法找到 WiFi");
    return "tom";
  }
  for (int i = 0; i < n; i++) {
    Serial.printf("[%d] %s (%ddBm)\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  int index = -1;
  while (index < 0 || index >= n) {
    Serial.print("請選擇 WiFi 編號：");
    while (Serial.available() == 0);
    index = Serial.parseInt();
  }
  String selectedSSID = WiFi.SSID(index);
  Serial.print("輸入密碼：");
  while (Serial.available()) Serial.read(); 
  while (Serial.available() == 0);
  String password = Serial.readStringUntil('\n');
  password.trim();

  WiFi.begin(selectedSSID.c_str(), password.c_str());
  Serial.println("\n嘗試連線...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi 連線成功");

  prefs.begin("wifi", false);
  prefs.putString("ssid", selectedSSID);
  prefs.putString("pwd", password);
  prefs.putString("server_ip", default_server_ip);
  prefs.end();

  return selectedSSID;
}

void handleMovement(float beta, float gamma, int duration) {
  if (abs(beta) < 10 && abs(gamma) < 10) {
    Serial.println("🛑 停止");
    stopMotors();
    if (duration > 0) delay(duration);
    return;
  }

  if (beta > 10) {
    Serial.println("⬆️ 前進");
    moveForward();
  } else if (beta < -10) {
    Serial.println("⬇️ 後退");
    moveBackward();
  }

  if (gamma > 20) {
    Serial.println("➡️ 右轉");
    turnRight();
  } else if (gamma < -20) {
    Serial.println("⬅️ 左轉");
    turnLeft();
  }

  if (duration > 0) {
    delay(duration);
    stopMotors();
  }
}

void stopMotors() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void moveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void moveBackward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void turnLeft() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnRight() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void backRadar() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = duration * 0.034 / 2;

  if (distance < 40) {
    tone(BUZZER, 1000, 100);
    delay(10 * distance * 0.4);
  } else {
    noTone(BUZZER);
  }

  if (distance > 0 && distance < 20) {
    Serial.println("距離過近，自動後退");
    while (distance < 20) {
      moveBackward();
      delay(200);
      stopMotors();

      digitalWrite(TRIG_PIN, LOW);
      delayMicroseconds(2);
      digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIG_PIN, LOW);
      duration = pulseIn(ECHO_PIN, HIGH);
      distance = duration * 0.034 / 2;
    }
    Serial.println("安全距離已恢復，繼續執行其他指令");
  }

  if (client.connected()) {
    client.print("DIST:");
    client.println(distance);
  }
  delay(100);
}
