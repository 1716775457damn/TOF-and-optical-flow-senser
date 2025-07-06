#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// 定义类型别名，兼容代码
typedef uint8_t u8;
typedef int16_t s16;

// 配置参数
const char* ssid = "ESP32-S3-Flow";  // AP名称
const char* password = "12345678";  // AP密码

// 光流传感器串口配置 - PV3953L1模块
#define FLOW_SENSOR_RX 18  // ESP32-S3的RX引脚连接到光流模块的T（发送）
#define FLOW_SENSOR_TX 17  // ESP32-S3的TX引脚连接到光流模块的R（接收）
#define FLOW_SENSOR_BAUD 19200  // 光流模块波特率

// Web服务器和WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// 用于控制WebSocket发送频率
unsigned long lastWsUpdateTime = 0;
const int wsUpdateInterval = 50; // 每50ms发送一次WebSocket数据

// 光流数据结构体
typedef struct {
  float flow_x, flow_y;    // 光流速度
  float flow_x_i, flow_y_i; // 光流积分位移
  u8 rotate_mode;          // 旋转模式
  u8 qual;                 // 数据质量
  u8 ok;                   // 数据有效性
  u8 en;                   // 使能状态
  u8 ssi;                  // SSI值
  u8 ssi_cnt;              // SSI计数
  u8 err;                  // 错误状态
  u8 yawMode;              // 偏航模式
  float height;            // 高度（毫米）
  uint32_t timestamp;      // 时间戳（毫秒）
} flow_t;

// 用于数据处理的结构体
typedef struct {
  float fix_x_i;    // X方向滤波处理后的积分位移
  float fix_y_i;    // Y方向滤波处理后的积分位移
  float ang_x;      // X方向姿态角补偿值
  float ang_y;      // Y方向姿态角补偿值
  float out_x_i;    // X方向最终位移输出
  float out_y_i;    // Y方向最终位移输出
  float x;          // X方向速度
  float y;          // Y方向速度
  float fix_x;      // X方向滤波处理后的速度
  float fix_y;      // Y方向滤波处理后的速度
  float out_x_i_o;  // 上一次X方向位移输出值
  float out_y_i_o;  // 上一次Y方向位移输出值
} pixel_flow_t;

// 姿态角数据结构体
typedef struct {
  float rol;  // 横滚角
  float pit;  // 俯仰角
} imu_data_t;

// 全局变量定义
flow_t mini;
pixel_flow_t pixel_flow;
imu_data_t imu_data = {0.0f, 0.0f}; // 初始化姿态角为0

// 定义角度转弧度转换因子
#define angle_to_rad 0.0174f
// 定义时间间隔
#define dT 0.02f // 假设为50Hz的更新频率

// 缓冲区
const int BUFFER_SIZE = 64;
uint8_t buffer[BUFFER_SIZE];
int bufferIndex = 0;

// WebSocket事件处理函数
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket客户端 #%u 已连接\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket客户端 #%u 已断开连接\n", client->id());
  }
}

// 初始化LittleFS文件系统
void initFS() {
  if (!LittleFS.begin(true)) {  // true参数会格式化文件系统如果挂载失败
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
  
  // 列出LittleFS中的文件
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  
  Serial.println("文件系统中的文件:");
  if (!file) {
    Serial.println("  没有找到文件！");
  }
  
  while (file) {
    Serial.print("  ");
    Serial.print(file.name());
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bytes)");
    file = root.openNextFile();
  }
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

// 光流数据接收处理函数
void Mini_Flow_Receive(u8 data) {
  static u8 RxBuffer[32];
  static u8 _data_cnt = 0;
  static u8 state = 0;
  static u8 fault_cnt;
  u8 sum = 0;

  switch (state) {
    case 0:
      if (data == 0xFE) {
        state = 1;
        RxBuffer[_data_cnt++] = data;
      } else
        state = 0;
      break;

    case 1:
      if (data == 0x04) {
        state = 2;
        RxBuffer[_data_cnt++] = data;
      } else
        state = 0;
      break;

    case 2:
      RxBuffer[_data_cnt++] = data;

      if (_data_cnt == 9) {
        state = 0;
        _data_cnt = 0;

        sum = (RxBuffer[2] + RxBuffer[3] + RxBuffer[4] + RxBuffer[5]);

        if (sum == RxBuffer[6]) { // 和校验
          mini.ssi_cnt++;

          // 读取原始数据
          mini.flow_x = ((s16)((*(RxBuffer + 3)) << 8) | *(RxBuffer + 2));
          mini.flow_y = ((s16)((*(RxBuffer + 5)) << 8) | *(RxBuffer + 4));
          mini.qual = *(RxBuffer + 7);
          
          // 获取高度数据（单位：厘米，转换为毫米）
          uint16_t height_cm = ((uint16_t)((RxBuffer[5] << 8) | RxBuffer[4]));
          mini.height = height_cm * 10.0f; // 厘米转毫米
          
          // 更新时间戳
          mini.timestamp = millis();

          // 累加求位移
          mini.flow_x_i += mini.flow_x; // X方向累加求位移
          mini.flow_y_i += mini.flow_y; // Y方向累加求位移
          
          // 调试输出
          Serial.print("光流 X: ");
          Serial.print(mini.flow_x);
          Serial.print(", Y: ");
          Serial.print(mini.flow_y);
          Serial.print(", 高度: ");
          Serial.print(mini.height);
          Serial.print(" mm, 质量: ");
          Serial.println(mini.qual);
        }
      }
      break;

    default:
      state = 0;
      _data_cnt = 0;
      break;
  }
}

// 光流数据处理函数
void process_optical_flow_data() {
  // 第1步：对积分位移进行简单的低通滤波
  pixel_flow.fix_x_i += (mini.flow_x_i - pixel_flow.fix_x_i) * 0.2;
  pixel_flow.fix_y_i += (mini.flow_y_i - pixel_flow.fix_y_i) * 0.2;

  // 第2步：用姿态角补偿积分位移
  pixel_flow.ang_x += (600.0f * tan(imu_data.rol * angle_to_rad) - pixel_flow.ang_x) * 0.2;
  pixel_flow.ang_y += (600.0f * tan(imu_data.pit * angle_to_rad) - pixel_flow.ang_y) * 0.2;
  pixel_flow.out_x_i = pixel_flow.fix_x_i - pixel_flow.ang_x;
  pixel_flow.out_y_i = pixel_flow.fix_y_i - pixel_flow.ang_y;

  // 第3步：对积分位移进行微分处理，得到速度
  pixel_flow.x = (pixel_flow.out_x_i - pixel_flow.out_x_i_o) / dT;
  pixel_flow.out_x_i_o = pixel_flow.out_x_i;
  pixel_flow.y = (pixel_flow.out_y_i - pixel_flow.out_y_i_o) / dT;
  pixel_flow.out_y_i_o = pixel_flow.out_y_i;

  // 低通滤波处理速度数据
  pixel_flow.fix_x += (pixel_flow.x - pixel_flow.fix_x) * 0.1f;
  pixel_flow.fix_y += (pixel_flow.y - pixel_flow.fix_y) * 0.1f;
}

// 准备WebSocket发送的JSON数据
void sendWebSocketData() {
  if (ws.count() > 0) {  // 只有当有客户端连接时才发送数据
    unsigned long now = millis();
    if (now - lastWsUpdateTime >= wsUpdateInterval) {
      lastWsUpdateTime = now;
      
      // 创建一个较小的JSON文档来优化传输
      DynamicJsonDocument doc(256);
      
      // 优化数据结构，只包含必要的字段
      doc["rx"] = (int)mini.flow_x;  // 使用缩写键名减小数据大小
      doc["ry"] = (int)mini.flow_y;
      doc["h"] = (int)mini.height;
      doc["q"] = mini.qual;
      doc["px"] = pixel_flow.fix_x;
      doc["py"] = pixel_flow.fix_y;
      doc["dx"] = pixel_flow.out_x_i;
      doc["dy"] = pixel_flow.out_y_i;
      
      String jsonString;
      serializeJson(doc, jsonString);
      ws.textAll(jsonString);  // 向所有连接的客户端发送数据
    }
  }
}

// 初始化光流传感器串口
void setupFlowSensor() {
  // 配置Serial2用于与光流模块通信
  Serial2.begin(FLOW_SENSOR_BAUD, SERIAL_8N1, FLOW_SENSOR_RX, FLOW_SENSOR_TX);
  
  // 初始化光流数据结构体
  memset(&mini, 0, sizeof(mini));
  memset(&pixel_flow, 0, sizeof(pixel_flow));
}

// 处理HTTP请求，返回当前光流数据的JSON格式
void handleFlowDataRequest(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);
  
  // 原始光流数据
  doc["raw_flow_x"] = mini.flow_x;
  doc["raw_flow_y"] = mini.flow_y;
  doc["height"] = mini.height;
  doc["quality"] = mini.qual;
  
  // 处理后的光流数据
  doc["processed_x"] = pixel_flow.fix_x;
  doc["processed_y"] = pixel_flow.fix_y;
  doc["displacement_x"] = pixel_flow.out_x_i;
  doc["displacement_y"] = pixel_flow.out_y_i;
  
  doc["timestamp"] = mini.timestamp;
  
  String response;
  serializeJson(doc, response);
  
  request->send(200, "application/json", response);
}

// 设置网页服务器
void setupWebServer() {
  // 配置WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  
  // 提供Web界面的HTML文件
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html");
      Serial.println("提供index.html页面");
    } else {
      // 如果文件系统中没有index.html，提供一个简单的内联HTML
      String htmlContent = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "  <meta charset='UTF-8'>"
        "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "  <title>光流传感器数据</title>"
        "  <style>"
        "    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; }"
        "    .card { background-color: #f0f0f0; border-radius: 10px; padding: 15px; margin: 15px 0; }"
        "    .value { font-size: 24px; font-weight: bold; color: #2c3e50; }"
        "  </style>"
        "</head>"
        "<body>"
        "  <h1>光流传感器数据</h1>"
        "  <div class='card'>"
        "    <h2>X方向光流: <span id='flow-x' class='value'>0</span></h2>"
        "  </div>"
        "  <div class='card'>"
        "    <h2>Y方向光流: <span id='flow-y' class='value'>0</span></h2>"
        "  </div>"
        "  <div class='card'>"
        "    <h2>高度 (mm): <span id='height' class='value'>0.0</span></h2>"
        "  </div>"
        "  <div class='card'>"
        "    <h2>数据质量: <span id='quality' class='value'>0</span></h2>"
        "  </div>"
        "  <script>"
        "    const ws = new WebSocket('ws://' + window.location.hostname + '/ws');"
        "    ws.onmessage = function(event) {"
        "      const data = JSON.parse(event.data);"
        "      document.getElementById('flow-x').textContent = data.rx;"
        "      document.getElementById('flow-y').textContent = data.ry;"
        "      document.getElementById('height').textContent = data.h;"
        "      document.getElementById('quality').textContent = data.q;"
        "    };"
        "  </script>"
        "</body>"
        "</html>";
      request->send(200, "text/html", htmlContent);
      Serial.println("提供内联HTML页面（文件系统中没有找到index.html）");
    }
  });
  
  // 提供CSS文件
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/style.css")) {
      request->send(LittleFS, "/style.css", "text/css");
      Serial.println("提供style.css文件");
    } else {
      request->send(404, "text/plain", "样式表未找到");
      Serial.println("style.css未找到");
    }
  });
  
  // 提供JavaScript文件
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/script.js")) {
      request->send(LittleFS, "/script.js", "application/javascript");
      Serial.println("提供script.js文件");
    } else {
      request->send(404, "text/plain", "脚本文件未找到");
      Serial.println("script.js未找到");
    }
  });
  
  // API端点，提供光流数据（保留HTTP接口作为备用）
  server.on("/data", HTTP_GET, handleFlowDataRequest);
  
  // 404错误处理
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "页面未找到");
    Serial.print("未找到请求的页面: ");
    Serial.println(request->url());
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
  
  // 设置Web服务器
  setupWebServer();
  
  Serial.println("初始化完成");
}

void loop() {
  // 读取光流传感器数据
  if (Serial2.available()) {
    uint8_t c = Serial2.read();
    Mini_Flow_Receive(c);
  }
  
  // 处理光流数据
  process_optical_flow_data();
  
  // 通过WebSocket发送数据
  sendWebSocketData();
  
  // 清理WebSocket连接
  ws.cleanupClients();
  
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
  
  delay(5); // 更高频率的循环，更及时的处理
} 