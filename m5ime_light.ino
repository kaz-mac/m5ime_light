/* 
  m5ime_light.ino
  M5Stack ATOM MatrixでPCのIMEの状態をLEDで表示するやつ

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  TODO:
    ESPNOWを使って子機を追加できるようにしたい
    発光パターンを選べるようにしたい（たぶんすぐ飽きるから落ち着いた発光パターンもあるとよい）
    IMEの状態に応じてサーボを動かしたい
*/
#include <M5Unified.h>
#include <FastLED.h>
#include <esp_now.h>
#include <WiFi.h>

// LEDの光らせ方の設定
const uint8_t GAMING_SPEED = 600;   // スピード
const uint8_t DELTA_HUE = 3;        // 色の密度
const uint8_t IME_ON_BRIGHT = 128;  // IMEがONのときの明るさ max 255
const uint8_t IME_OFF_BRIGHT = 10;  // IMEがOFFのときの明るさ max 255
const CRGB IME_OFF_COLOR = CRGB::Blue;  // IMEがOFFのときの色

// 親機と子機の設定（子機がない場合は子機の設定は不要）
#define IS_MASTER       true      // 親機の場合はtrue、子機の場合はfalse
const bool USE_ESPNOW = true;     // 子機と同期させる場合true
const uint8_t SLAVE_MAC_ADDRESS_1[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };   // 子機のMACアドレス

// 親機の設定（FastLEDではM5.getPin()が使えないので個別に設定する）
#if IS_MASTER == true
  // for ATOM S3U
  const bool   USE_INTERNAL = true; // 本体LEDを有効にする
  const size_t INLED_NUM  = 1;      // 本体LEDの数
  const bool   USE_EXTERNAL = false;// 外部LED(Port A)を有効にする
  const size_t EXLED_NUM = 30;      // 外部LEDの数
  const int    GPIO_INLED = 35;     // 本体LEDのGPIOポート（ATOM=27、ATOMS3=35）
  const int    GPIO_EXLED = 2;      // 外部LED(Port A黄)のGPIOポート（ATOM=26、ATOMS3=2）
#endif

// 子機の設定（子機を使わない場合はこのままでよい）
#if IS_MASTER == false
  // for ATOM Matrix
  const bool   USE_INTERNAL = true; // 本体LEDを有効にする
  const size_t INLED_NUM  = 25;     // 本体LEDの数
  const bool   USE_EXTERNAL = true; // 外部LED(Port A)を有効にする
  const size_t EXLED_NUM = 30;      // 外部LEDの数
  const int    GPIO_INLED = 27;     // 本体LEDのGPIOポート（ATOM=27、ATOMS3=35）
  const int    GPIO_EXLED = 26;     // 外部LED(Port A黄)のGPIOポート（ATOM=26、ATOMS3=2）
#endif

// グローバル変数
CRGB ledsIn[INLED_NUM];
CRGB ledsEx[EXLED_NUM];
uint8_t ime = 0;   // 0=消灯 1=IMEオフ 2=IMEオン

// 子機：データの受信
void onEspnowReceive(const uint8_t* mac_addr, const uint8_t* data, int data_len) {
  if (data_len == 4 && data[0] == 0x49 && data[1] == 0x4D && data[2] == 0x45) {
    Serial.println("receive "+String(data[3])+" from master");
    ime = (data[3] <= 2) ? data[3] : 0;
  }  
}

// 親機：データの送信
void onEspnowSend(const uint8_t* mac_addr, esp_now_send_status_t status) {
  Serial.println("sent");
}

// 初期化
void setup() {
  M5.begin();
  Serial.begin(115200);
  delay(200);
  Serial.println("start!");
  Serial.println(WiFi.macAddress());

  // ESPNOW初期化
  if (USE_ESPNOW) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() == ESP_OK) Serial.println("ESPNow Ready");
    else Serial.println("ESPNow error");
    if (IS_MASTER) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, SLAVE_MAC_ADDRESS_1, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      if (esp_now_add_peer(&peerInfo) == ESP_OK) Serial.println("Peer OK");
      else Serial.println("Peer error");
      esp_now_register_send_cb(onEspnowSend);
    } else {
      esp_now_register_recv_cb(onEspnowReceive);
    }
  }

  // GPIOとFastLEDの設定
  if (USE_INTERNAL) {
    pinMode(GPIO_INLED, OUTPUT);
    FastLED.addLeds<WS2811, GPIO_INLED, GRB>(ledsIn, INLED_NUM);
  }
  if (USE_EXTERNAL) {
    pinMode(GPIO_EXLED, OUTPUT);
    FastLED.addLeds<WS2811, GPIO_EXLED, GRB>(ledsEx, EXLED_NUM);
  }

  // 開始の合図と待機
  FastLED.setBrightness(IME_ON_BRIGHT);
  if (USE_INTERNAL) {
    fill_solid(ledsIn, INLED_NUM, CRGB::White);
  }
  if (USE_EXTERNAL) {
    fill_solid(ledsEx, EXLED_NUM, CRGB::White);
  }
  FastLED.show();
  delay(500);
  fill_solid(ledsEx, EXLED_NUM, CRGB::Black);
  FastLED.show();
}

// ループ
void loop() {
  static float brightness = 1.0;
  static bool debugMode = false;
  M5.update();

  // 共通：ボタンで明るさ変更
  if (M5.BtnA.wasPressed()) {
    brightness -= 0.2;
    if (brightness < 0) brightness = 1.0;
  }

  // 親機：ボタン長押しで強制ゲーミングモード
  if (IS_MASTER && M5.BtnA.pressedFor(1000)) {
    while (M5.BtnA.isPressed()) {
      delay(5);
      M5.update();
    }
    debugMode = !debugMode;
    ime = (ime == 2) ? 1 : 2;
  }

  // 親機：IMEの状態をシリアルポートから受信する
  String serstr;
  if (IS_MASTER && !debugMode && Serial.available() > 0) {
    serstr = Serial.readStringUntil('\n');
    serstr.trim();
    if (serstr == "JA") ime = 2;
    else if (serstr == "EN") ime = 1;
    else if (serstr == "SL") ime = 0;
  }

  // 親機：ESPNOWでデータを送信
  if (USE_ESPNOW && IS_MASTER) {
    static uint8_t imeLast = -1;
    if (ime != imeLast) {
      uint8_t data[4] = { 0x49, 0x4D, 0x45, ime }; // 送信データ
      esp_err_t result = esp_now_send(SLAVE_MAC_ADDRESS_1, data, sizeof(data));
      Serial.println("send "+String(data[3])+" to slave");
      imeLast = ime;
    }
  }

  // 共通：IMEの状態によってLEDの点灯パターンを変える
  if (ime == 2) {
    // IMEがON
    uint8_t hue = beat8(GAMING_SPEED, 255); 
    if (USE_INTERNAL) fill_rainbow(ledsIn, INLED_NUM, hue, DELTA_HUE);            
    if (USE_EXTERNAL) fill_rainbow(ledsEx, EXLED_NUM, hue, DELTA_HUE);            
    FastLED.setBrightness(IME_ON_BRIGHT * brightness);
  } else if (ime == 1) {
    // IMEがOFF
    if (USE_INTERNAL) fill_solid(ledsIn, INLED_NUM, IME_OFF_COLOR);
    if (USE_EXTERNAL) fill_solid(ledsEx, EXLED_NUM, IME_OFF_COLOR);
    FastLED.setBrightness(IME_OFF_BRIGHT * brightness);
  } else if (ime == 0) {
    // 消灯
    if (USE_INTERNAL) fill_solid(ledsIn, INLED_NUM, CRGB::Black);
    if (USE_EXTERNAL) fill_solid(ledsEx, EXLED_NUM, CRGB::Black);
    FastLED.setBrightness(0);
  }
  FastLED.show();
  delay(20);
}
