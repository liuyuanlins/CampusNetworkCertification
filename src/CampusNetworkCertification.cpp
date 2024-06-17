#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESP32FtpServer.h>
#include "CameraApp.h"

#include <DNSServer.h>

#include "FS.h"     // SD Card ESP32
#include "SD_MMC.h" // SD Card ESP32
#include "SPI.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// URLs and credentials for the login process
const char *url_header = "http://222.197.192.59:9090/zportal/loginForWeb?wlanuserip=5832eab1f630bcdaac19f3d11ad8443c&wlanacname=c8c9622958c8e70501e9284a0abec0fc&ssid=bfd0fd2d574e31c6ba728e0a908cdb8f&nasip=cbde5ddbb5eb03be513e83acf881fb36&snmpagentip=&mac=f862e1a8940dcdac7e81a799bdfc3b57&t=wireless-v2&url=5eabee93b202dc1e55830c33c71804648675c3c7527f3a7112bcb242b5353b5d24a7b21043784bbc&apmac=&nasid=c8c9622958c8e70501e9284a0abec0fc&vid=0147bdf913dc29b3&port=0c8b5612931ecde4&nasportid=b8007b401a20ea61501893ea5c542842d357e7263d72d72c0ebc4d08a33b5660"; // Replace with actual URL
const char *url_login = "http://222.197.192.59:9090/zportal/login/do";
// 创建 JSON 对象
JsonDocument paramJsonDoc;

// 创建Web服务器对象
WebServer server(80);
Preferences preferences;

// 定义DNS服务器
// DNSServer dnsServer;
// 设置服务器端口
const byte DNS_PORT = 53;

File fsUploadFile;

FtpServer ftpSrv;
// HTML 表单页面模板
const char *htmlFormTemplate = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>配置页面</title>
  <style>
    body { 
      font-family: Arial, sans-serif; 
      background-color: #f4f4f9; 
      margin: 0; 
      padding: 0; 
      display: flex; 
      justify-content: center; 
      align-items: center; 
      height: 100vh; 
    }
    .container {
      background-color: #fff; 
      padding: 20px 40px; 
      border-radius: 10px; 
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); 
      max-width: 400px; 
      width: 100%; 
    }
    h2 {
      text-align: center; 
      color: #333; 
    }
    label { 
      display: block; 
      margin-top: 10px; 
      color: #555; 
    }
    input[type="text"], input[type="password"] { 
      width: calc(100% - 16px); 
      padding: 8px; 
      margin-top: 5px; 
      border: 1px solid #ccc; 
      border-radius: 5px; 
      box-sizing: border-box; 
    }
    input[type="submit"], .camera-button { 
      margin-top: 20px; 
      padding: 10px 20px; 
      border: none; 
      border-radius: 5px; 
      background-color: #007BFF; 
      color: #fff; 
      font-size: 16px; 
      cursor: pointer; 
      width: 100%; 
    }
    input[type="submit"]:hover, .camera-button:hover { 
      background-color: #0056b3; 
    }
    .checkbox-container { 
      margin-top: 10px; 
      display: flex; 
      align-items: center; 
    }
    .checkbox-label { 
      margin-right: 10px; 
      color: #555; 
    }
    .button-group {
      display: flex;
      flex-direction: column;
      gap: 10px; /* 间距 */
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>配置页面</h2>
    <form action="/save" method="POST">
      <label for="ssid">WiFi 热点名称:</label>
      <input type="text" id="ssid" name="ssid" value="%SSID%" required>
      <label for="password">WiFi 热点密码:</label>
      <input type="password" id="password" name="password" value="%PASSWORD%" required>
      <label for="username">用户名:</label>
      <input type="text" id="username" name="username" value="%USERNAME%" required>
      <label for="userpassword">用户密码:</label>
      <input type="password" id="userpassword" name="userpassword" value="%USERPASSWORD%" required>
      <div class="checkbox-container">
        <input type="checkbox" id="network" name="network" value="internal" %NETWORK_CHECKED%>
        <label for="network" class="checkbox-label">内网</label>
      </div>
      <div class="button-group">
        <input type="submit" value="配置">
        <button type="button" class="camera-button" onclick="location.href='/camera'">打开摄像头</button>
        <button type="button" class="camera-button" onclick="location.href='/explorer'">打开文件浏览器</button>
      </div>
    </form>
  </div>
</body>
</html>
)rawliteral";

void connectToWiFi()
{

  // WiFi credentials
  String ssid = paramJsonDoc["ssid"];
  String password = paramJsonDoc["password"];

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password, 0, NULL, true); // Connect to the hidden network

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");

  // Log WiFi connection details
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Subnet Mask: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("Gateway IP: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("DNS IP: ");
  Serial.println(WiFi.dnsIP());
}

String get_form_data()
{
  Serial.println("Fetching form data...");
  HTTPClient http;
  http.begin(url_header);
  int httpCode = http.GET();

  if (httpCode > 0)
  {
    String payload = http.getString();
    Serial.println("Form data fetched successfully");

    int formStart = payload.indexOf("<form id=\"login_form\"");
    int formEnd = payload.indexOf("</form>", formStart);

    if (formStart == -1 || formEnd == -1)
    {
      http.end();
      Serial.println("Form not found in HTML");
      return "";
    }

    String formHTML = payload.substring(formStart, formEnd + 7); // Include the closing form tag

    // 创建 JSON 对象
    JsonDocument jsonDoc;

    // 解析所有 input 元素
    int inputPos = formHTML.indexOf("<input");
    while (inputPos != -1)
    {
      int inputEnd = formHTML.indexOf(">", inputPos);
      String inputTag = formHTML.substring(inputPos, inputEnd + 1);

      int namePos = inputTag.indexOf("name=\"");
      if (namePos != -1)
      {
        int nameEnd = inputTag.indexOf("\"", namePos + 6);
        String name = inputTag.substring(namePos + 6, nameEnd);

        int valuePos = inputTag.indexOf("value=\"");
        String value = "";
        if (valuePos != -1)
        {
          int valueEnd = inputTag.indexOf("\"", valuePos + 7);
          value = inputTag.substring(valuePos + 7, valueEnd);
        }

        // 将其他字段添加到 JSON 对象中
        if (name != "username" && name != "pwd" && name != "")
        {
          jsonDoc[name] = value;
        }
      }

      inputPos = formHTML.indexOf("<input", inputEnd);
    }

    // 从第一个服务选项获取默认的 serviceId
    int serviceIdStart = payload.indexOf("id=\"serviceListUl\"");
    if (serviceIdStart != -1)
    {
      int spanStart = payload.indexOf("<span", serviceIdStart);
      if (spanStart != -1)
      {
        int valuePos = payload.indexOf("value=\"", spanStart);
        if (valuePos != -1)
        {
          int valueEnd = payload.indexOf("\"", valuePos + 7);
          String serviceId = payload.substring(valuePos + 7, valueEnd);
          jsonDoc["serviceId"] = serviceId;
          Serial.println("Found serviceId: " + serviceId);
        }
        else
        {
          Serial.println("No value attribute found in span tag");
        }
      }
      else
      {
        Serial.println("No span tag found in serviceListUl");
      }
    }
    else
    {
      Serial.println("No serviceListUl found in HTML");
    }

    http.end();

    String username = paramJsonDoc["username"];
    String userpassword = paramJsonDoc["userpassword"];
    String network = paramJsonDoc["network"];

    jsonDoc["qrCodeId"] = "请输入编号";
    jsonDoc["username"] = username;
    jsonDoc["pwd"] = userpassword;
    jsonDoc["validCode"] = "验证码";
    jsonDoc["validCodeFlag"] = "false";
    jsonDoc["serviceId"] = "";

    // 将 JSON 对象序列化为 formData 字符串
    String formData;
    for (JsonPair kv : jsonDoc.as<JsonObject>())
    {
      if (formData.length() > 0)
      {
        formData += "&";
      }
      formData += kv.key().c_str();
      formData += "=";
      formData += kv.value().as<String>();
    }

    // Print the fetched form data
    Serial.println("Fetched form data:");
    Serial.println(formData);

    return formData;
  }
  else
  {
    Serial.println("HTTP request failed with code: " + String(httpCode));
  }

  http.end();
  Serial.println("Error fetching form data");
  return "";
}

bool authenticated_connection()
{
  Serial.println("Authenticating connection...");
  HTTPClient http;
  http.begin(url_login);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String formData = get_form_data();
  if (formData == "")
  {
    return false;
  }

  int httpCode = http.POST(formData);
  if (httpCode > 0)
  {
    String response = http.getString();
    Serial.print("Authentication response: ");
    Serial.println(response);
    if (response.indexOf("success") != -1 || response.indexOf("online") != -1)
    {
      http.end();
      return true;
    }
  }

  http.end();
  Serial.println("Error during authentication");
  return false;
}

// 检查 WiFi 是否连接
bool is_connected()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin("https://www.baidu.com"); // 使用 HTTPS 而不是 HTTP
    int httpCode = http.GET();

    // 检查 HTTP 响应码是否为 200
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();

      // 打印 HTTP 响应的内容
      // Serial.println("HTTP 响应内容:");
      // Serial.println(payload);

      // 检查响应内容中是否包含 "百度" 字样
      bool found = payload.indexOf("baidu") != -1;

      // 打印检查结果
      if (found)
      {
        Serial.println("成功找到 '百度' 字样");
      }
      else
      {
        Serial.println("未找到 '百度' 字样");
      }

      http.end();
      return found;
    }
    else
    {
      Serial.print("HTTP 请求失败，错误码: ");
      Serial.println(httpCode);
    }
    http.end();
  }
  else
  {
    Serial.println("WiFi 未连接");
  }

  return false;
}

// 处理表单页面请求
void handleConfig()
{
  preferences.begin("config", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  String username = preferences.getString("username", "");
  String userpassword = preferences.getString("userpassword", "");
  String network = preferences.getString("network", "external");
  preferences.end();

  String htmlForm = htmlFormTemplate;
  htmlForm.replace("%SSID%", ssid);
  htmlForm.replace("%PASSWORD%", password);
  htmlForm.replace("%USERNAME%", username);
  htmlForm.replace("%USERPASSWORD%", userpassword);
  htmlForm.replace("%NETWORK_CHECKED%", network == "internal" ? "checked" : "");

  server.send(200, "text/html", htmlForm);
}

// 处理表单提交并保存参数
void handleSave()
{
  if (server.method() == HTTP_POST)
  {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String username = server.arg("username");
    String userpassword = server.arg("userpassword");
    String network = server.arg("network");

    if (network != "internal")
    {
      network = "external";
    }

    // 保存参数到Preferences
    preferences.begin("config", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putString("username", username);
    preferences.putString("userpassword", userpassword);
    preferences.putString("network", network);
    preferences.end();

    Serial.println("保存的参数:");
    Serial.print("WiFi 热点名称: ");
    Serial.println(ssid);
    Serial.print("WiFi 热点密码: ");
    Serial.println(password);
    Serial.print("用户名: ");
    Serial.println(username);
    Serial.print("用户密码: ");
    Serial.println(userpassword);
    Serial.print("网络类型: ");
    Serial.println(network);

    server.send(200, "text/html", "<meta charset=\"UTF-8\">配置保存成功！");
  }
  else
  {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void reloadParam()
{
  // 读取并打印保存的参数
  preferences.begin("config", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  String username = preferences.getString("username", "");
  String userpassword = preferences.getString("userpassword", "");
  String network = preferences.getString("network", "");
  preferences.end();

  if (ssid.isEmpty())
  {
    paramJsonDoc["ssid"] = "CCCP";
  }
  else
  {
    paramJsonDoc["ssid"] = ssid;
  }

  if (password.isEmpty())
  {
    paramJsonDoc["password"] = "64456445";
  }
  else
  {
    paramJsonDoc["password"] = password;
  }
  paramJsonDoc["username"] = username;
  paramJsonDoc["userpassword"] = userpassword;
  paramJsonDoc["network"] = network;

  Serial.println("保存的参数:");
  Serial.print("WiFi 热点名称: ");
  Serial.println(ssid);
  Serial.print("WiFi 热点密码: ");
  Serial.println(password);
  Serial.print("用户名: ");
  Serial.println(username);
  Serial.print("用户密码: ");
  Serial.println(userpassword);
  Serial.print("网络类型: ");
  Serial.println(network);
}

void FSExplorerInit()
{
  if (!SD_MMC.begin())
  {
    Serial.println("SD Card Mount Failed");
    server.on("/explorer", handleNoSD);
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  // Check if SD card is present and can be initialized
  if (cardType == CARD_NONE)
  {
    Serial.println("No SD Card attached");
    server.on("/explorer", handleNoSD);
    return;
  }

  server.on("/explorer", handleFileExplorer);
  server.on("/download", handleFileDownload);
  server.on(
      "/upload", HTTP_POST, []()
      { server.send(200, "text/plain", ""); },
      handleFileUpload);

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC)
  {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD)
  {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    Serial.println("SDHC");
  }
  else
  {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

// Helper function to URL-encode a string
String urlEncode(const String &str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0x0F) + '0';
      if ((c & 0x0F) > 9)
      {
        code1 = (c & 0x0F) - 10 + 'A';
      }
      code0 = ((c >> 4) & 0x0F) + '0';
      if (((c >> 4) & 0x0F) > 9)
      {
        code0 = ((c >> 4) & 0x0F) - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

// Helper function to URL-decode a string
String urlDecode(const String &str)
{
  String decodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == '+')
    {
      decodedString += ' ';
    }
    else if (c == '%')
    {
      code0 = str.charAt(++i);
      code1 = str.charAt(++i);
      c = (hexToDec(code0) << 4) | hexToDec(code1);
      decodedString += c;
    }
    else
    {
      decodedString += c;
    }
  }
  return decodedString;
}

// Helper function to convert hex character to decimal
char hexToDec(char c)
{
  if ('0' <= c && c <= '9')
  {
    return c - '0';
  }
  else if ('a' <= c && c <= 'f')
  {
    return c - 'a' + 10;
  }
  else if ('A' <= c && c <= 'F')
  {
    return c - 'A' + 10;
  }
  return 0;
}

// Helper function to determine content type based on file extension
String getContentType(const String &path)
{
  if (path.endsWith(".txt") || path.endsWith(".h") || path.endsWith(".hpp") || path.endsWith(".c") || path.endsWith(".cpp"))
    return "text/plain; charset=utf-8";
  else if (path.endsWith(".htm") || path.endsWith(".html"))
    return "text/html; charset=utf-8";
  else if (path.endsWith(".css"))
    return "text/css";
  else if (path.endsWith(".js"))
    return "application/javascript";
  else if (path.endsWith(".png"))
    return "image/png";
  else if (path.endsWith(".jpg") || path.endsWith(".jpeg"))
    return "image/jpeg";
  else if (path.endsWith(".gif"))
    return "image/gif";
  // else if (path.endsWith(".mp4")) return "video/mp4";
  else if (path.endsWith(".mp3"))
    return "audio/mpeg";
  else if (path.endsWith(".ogg"))
    return "audio/ogg";
  else if (path.endsWith(".wav"))
    return "audio/wav";
  else if (path.endsWith(".webm"))
    return "video/webm";
  else if (path.endsWith(".mkv"))
    return "video/x-matroska";
  return "application/octet-stream";
}

// Helper function to determine if the content type should be inline preview
bool isInlinePreview(const String &contentType)
{
  return contentType.startsWith("text/") || contentType.startsWith("image/") || contentType == "application/javascript" || contentType.startsWith("video/") || contentType.startsWith("audio/");
}

void handleFileDownload()
{
  if (server.hasArg("file"))
  {
    String path = server.arg("file");
    Serial.println("Download request for file: " + path);
    path = "/" + urlDecode(path); // Decode the URL-encoded file name
    File downloadFile = SD_MMC.open(path, FILE_READ);
    if (downloadFile)
    {
      String contentType = getContentType(path);
      bool inlinePreview = isInlinePreview(contentType);

      server.sendHeader("Content-Type", contentType);

      // Ensure the filename is properly encoded for UTF-8
      String encodedFilename = String(path.substring(1));

      if (inlinePreview)
      {
        server.sendHeader("Content-Disposition", "inline; filename*=UTF-8''" + encodedFilename);
      }
      else
      {
        server.sendHeader("Content-Disposition", "attachment; filename*=UTF-8''" + encodedFilename);
      }

      server.streamFile(downloadFile, contentType);
      downloadFile.close();
    }
    else
    {
      Serial.println("File not found: " + path);
      server.send(404, "text/plain", "File not found");
    }
  }
  else
  {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleFileExplorer()
{
  String path = "/";
  if (server.hasArg("dir"))
  {
    path = server.arg("dir");
  }
  File root = SD_MMC.open(path);
  if (!root)
  {
    server.send(404, "text/plain", "Directory not found");
    return;
  }
  if (!root.isDirectory())
  {
    server.send(404, "text/plain", "Not a directory");
    return;
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>File Explorer</title>
<style>
body {
    font-family: Arial, sans-serif;
    background-color: #f8f9fa;
    color: #343a40;
}
h1 {
    color: #007bff;
}
ul {
    list-style-type: none;
    padding: 0;
}
li {
    margin: 5px 0;
}
a {
    text-decoration: none;
    color: #007bff;
}
a:hover {
    text-decoration: underline;
}
form {
    margin-top: 20px;
}
input[type='file'] {
    display: none;
}
.custom-file-upload {
    display: inline-block;
    padding: 10px 20px;
    cursor: pointer;
    background-color: #007bff;
    color: white;
    border: none;
    border-radius: 4px;
    font-size: 16px;
    margin: 4px 2px;
    text-align: center;
    text-decoration: none;
}
.custom-file-upload:hover {
    background-color: #0056b3;
}
input[type='submit'] {
    background-color: #007bff;
    color: white;
    border: none;
    padding: 10px 20px;
    text-align: center;
    text-decoration: none;
    display: inline-block;
    font-size: 16px;
    margin: 4px 2px;
    cursor: pointer;
    border-radius: 4px;
}
input[type='submit']:hover {
    background-color: #0056b3;
}
#file-name {
    margin-left: 10px;
    font-size: 16px;
}
</style>
<script>
function updateFileName() {
    var input = document.getElementById('file-upload');
    var fileName = input.files.length > 0 ? input.files[0].name : '未选择任何文件';
    document.getElementById('file-name').textContent = fileName;
}
</script>
</head>
<body>
<h1>File Explorer</h1>
<ul>
)rawliteral";

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      html += "<li><a href=\"/explorer?dir=" + String(file.name()) + "\">" + String(file.name()) + "/</a></li>";
    }
    else
    {
      String encodedFileName = urlEncode(file.name());
      html += "<li><a href=\"/download?file=" + encodedFileName + "\">" + String(file.name()) + "</a></li>";
    }
    file = root.openNextFile();
  }

  html += R"rawliteral(
</ul>
<form method="POST" action="/upload" enctype="multipart/form-data">
    <label for="file-upload" class="custom-file-upload">选择文件</label>
    <input id="file-upload" type="file" name="file" onchange="updateFileName()">
    <span id="file-name">未选择任何文件</span>
    <input type="submit" value="Upload">
</form>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleFileUpload()
{
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    String filename = upload.filename;
    filename = "/" + filename;
    Serial.printf("Upload File Name: %s\n", filename.c_str());
    fsUploadFile = SD_MMC.open(filename, FILE_WRITE);
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (fsUploadFile)
    {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    {
      fsUploadFile.close();
    }
    server.send(200, "text/plain", "File Uploaded Successfully");
  }
  else
  {
    server.send(500, "text/plain", "File Upload Failed");
  }
}

void handleNoSD()
{
  String html = "<html><head><style>";
  html += "body { font-family: Arial, sans-serif; background-color: #f8f9fa; color: #343a40; text-align: center; padding-top: 50px; }";
  html += "h1 { color: #dc3545; }";
  html += "</style></head><body>";
  html += "<h1>SD Card Not Found</h1>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);                      // Initialize serial communication at 115200 baud rate

  // 设置ESP32作为隐藏的AP热点
  // WiFi.softAP("ESP32", "12345678", 1, true);
  // Serial.println("AP 模式已启动");
  // Serial.print("IP 地址: ");
  // Serial.println(WiFi.softAPIP());

  reloadParam();
  connectToWiFi();
  FSExplorerInit();

  // 初始化摄像头
  setupCamera();

  // 启动Web服务器
  server.on("/", handleConfig);
  server.on("/save", handleSave);
  server.begin();
  Serial.println("Web 服务器已启动");
  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.println(WiFi.localIP());

  startCameraServer();

  // 配置DNS服务器
  // dnsServer.start(DNS_PORT, "www.lin.com", WiFi.localIP());

  // 初始化 FTP 服务器
  ftpSrv.begin("admin", "admin"); // 设置 FTP 用户名和密码
}

// Timer for the loop delay
unsigned long previousMillis = 0;
const long interval = 4000; // interval in milliseconds

void loop()
{
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;

    static int try_count = 0;
    try_count++;

    if (is_connected())
    {
      Serial.printf("Try: %d - Network connected\n", try_count);
    }
    else
    {
      Serial.printf("Try: %d - Network disconnected\n", try_count);
      if (authenticated_connection())
      {
        Serial.println("Successfully reconnected");
      }
      else
      {
        Serial.println("Reconnection failed");
      }
    }
  }

  // 处理客户端请求
  server.handleClient();
  // 处理DNS请求
  // dnsServer.processNextRequest();
  // 处理 FTP 请求
  ftpSrv.handleFTP();
}