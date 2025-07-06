#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// 配置参数
const char* ssid = "ESP32-S3-Flow";  // AP名称
const char* password = "12345678";  // AP密码

// 光流传感器串口配置 - PV3953L1模块
#define FLOW_SENSOR_RX 18  // ESP32-S3的RX引脚连接到光流模块的T（发送）
#define FLOW_SENSOR_TX 17  // ESP32-S3的TX引脚连接到光流模块的R（接收）
#define FLOW_SENSOR_BAUD 19200  // 光流模块波特率

// Web服务器端口
AsyncWebServer server(80);

// 数据结构
struct FlowData {
  int16_t flow_x;          // X方向光流
  int16_t flow_y;          // Y方向光流
  float height;            // 高度（毫米）
  uint8_t quality;         // 数据质量
  uint32_t timestamp;      // 时间戳（毫秒）
} flowData;

// 缓冲区
const int BUFFER_SIZE = 64;
uint8_t buffer[BUFFER_SIZE];
int bufferIndex = 0;

// 初始化LittleFS文件系统
void initFS() {
  if (!LittleFS.begin()) {
    Serial.println("文件系统挂载失败，正在格式化...");
    if (!LittleFS.format()) {
      Serial.println("文件系统格式化失败");
      return;
    }
    if (!LittleFS.begin()) {
      Serial.println("文件系统挂载失败，即使在格式化后");
      return;
    }
  }
  Serial.println("文件系统初始化成功");
}

// 设置AP模式WiFi
void connectWiFi() {
  Serial.print("设置AP模式，SSID: ");
  Serial.println(ssid);
  
  // 设置固定IP地址
  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  // 配置AP模式
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, password);
  
  Serial.println("");
  Serial.println("WiFi AP模式启动成功");
  Serial.print("IP地址: ");
  Serial.println(WiFi.softAPIP());
}

// 解析光流数据 - 针对PV3953L1模块协议
bool parseFlowData(uint8_t* buffer, int length) {
  // 数据包格式: 包头(0xFE) + 字节数(0x04) + DATA0~DATA5(6字节) + 校验和 + 质量 + 包尾(0xAA)
  
  // 检查最小包长度
  if (length < 10) return false;
  
  // 检查包头
  if (buffer[0] != 0xFE || buffer[1] != 0x04) return false;
  
  // 检查包尾
  if (buffer[9] != 0xAA) return false;
  
  // 计算校验和 (DATA0~DATA5累加)
  uint8_t checksum = 0;
  for (int i = 2; i <= 7; i++) {
    checksum += buffer[i];
  }
  
  // 验证校验和
  if (checksum != buffer[8]) return false;
  
  // 解析光流数据 (X方向和Y方向)
  flowData.flow_x = (int16_t)((buffer[3] << 8) | buffer[2]);
  flowData.flow_y = (int16_t)((buffer[5] << 8) | buffer[4]);
  
  // 解析高度数据 (单位为厘米，转换为毫米)
  uint16_t height_cm = (uint16_t)((buffer[7] << 8) | buffer[6]);
  flowData.height = height_cm * 10.0; // 厘米转毫米
  
  // 获取地面环境质量值
  flowData.quality = buffer[8];
  
  // 更新时间戳
  flowData.timestamp = millis();
  
  return true;
}

// 初始化光流传感器串口
void setupFlowSensor() {
  // 配置Serial2用于与光流模块通信
  Serial2.begin(FLOW_SENSOR_BAUD, SERIAL_8N1, FLOW_SENSOR_RX, FLOW_SENSOR_TX);
}

// 处理HTTP请求，返回当前光流数据的JSON格式
void handleFlowDataRequest(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);
  
  doc["flow_x"] = flowData.flow_x;
  doc["flow_y"] = flowData.flow_y;
  doc["height"] = flowData.height;
  doc["quality"] = flowData.quality;
  doc["timestamp"] = flowData.timestamp;
  
  String response;
  serializeJson(doc, response);
  
  request->send(200, "application/json", response);
}

// 设置网页服务器
void setupWebServer() {
  // 提供Web界面的HTML文件
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  // 提供CSS文件
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  
  // 提供JavaScript文件
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });
  
  // API端点，提供光流数据
  server.on("/data", HTTP_GET, handleFlowDataRequest);
  
  // 404错误处理
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "页面未找到");
  });
  
  server.begin();
  Serial.println("HTTP服务器已启动");
}

void setup() {
  // 初始化串口以进行调试
  Serial.begin(115200);
  Serial.println("\n\n开始初始化...");
  
  // 初始化文件系统
  initFS();
  
  // 连接WiFi
  connectWiFi();
  
  // 设置光流传感器串口
  setupFlowSensor();
  
  // 初始化光流数据
  memset(&flowData, 0, sizeof(flowData));
  
  // 设置Web服务器
  setupWebServer();
  
  Serial.println("初始化完成");
}

void loop() {
  // 读取光流传感器数据
  if (Serial2.available()) {
    uint8_t c = Serial2.read();
    
    // 如果找到帧起始标识符，重置缓冲区
    if (c == 0xFE && bufferIndex == 0) {
      buffer[bufferIndex++] = c;
    }
    // 否则继续填充缓冲区
    else if (bufferIndex > 0) {
      buffer[bufferIndex++] = c;
      
      // 如果找到帧结束标识符或缓冲区达到预期长度，处理数据帧
      if (c == 0xAA && bufferIndex >= 10) {
        if (parseFlowData(buffer, bufferIndex)) {
          Serial.print("光流 X: ");
          Serial.print(flowData.flow_x);
          Serial.print(", Y: ");
          Serial.print(flowData.flow_y);
          Serial.print(", 高度: ");
          Serial.print(flowData.height);
          Serial.print(" mm, 质量: ");
          Serial.println(flowData.quality);
        }
        bufferIndex = 0;
      }
      
      // 防止缓冲区溢出
      if (bufferIndex >= BUFFER_SIZE) {
        bufferIndex = 0;
      }
    }
  }
  
  // 检查AP是否正常运行
  if (WiFi.softAPgetStationNum() >= 0) {
    // 每10秒输出一次当前连接的设备数
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 10000) {
      Serial.print("当前连接设备数: ");
      Serial.println(WiFi.softAPgetStationNum());
      lastCheck = millis();
    }
  }
  
  delay(10); // 小延迟以减少CPU负载
} 