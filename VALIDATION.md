# v1.0.33 驗證紀錄

## 本機回歸：16 組通過

- core、vendor_core、capture、driver_recovery、idf_driver、idf_packet、identity、pairing_led、communication、uart_backlog。
- matter_bridge、matter_fan、matter_window、matter_sdk_routing、matter_thermostat。
- portal（完整 production Portal，載入本機 ESPHome save_wifi_sta／set_sta／connect_soon_ 方法；執行時提供 --esphome-root）。
- 真驅動 UART backlog 測試在原 v1.0.32 可重現失敗；新版涵蓋 1–4 段、32 次連按、REV／跳脫、資料分批到達、getter 順序與 TX ACK。
- 核心涵蓋 100 次未知風速→低風速恢復、發送中排入新溫度／關機、配對與 NVS 布局、原廠接收無 IR 重送及逾時不自動重送。
- Thermostat 測試從鎖定 SDK 抽出 enum 與 SystemMode 驗證分支，重現 CoolingOnly 拒絕 Heat；確認新產生器、升級 on_boot 修正順序。
- 配對視窗涵蓋首次與追加的 pending／rollback／retry、真正完成才隱藏、逾時及清除紀錄。
- 直接抽取 YAML 溫度轉換及實際屬性 writer，測有效值→NaN／無限值／越界 null→恢復。

## 瀏覽器

Chromium v33 模擬裝置回歸通過，新增『選單保持焦點仍顯示 SSE 實際風速』；舊 v32 最小案例確實失敗。其他涵蓋配對／候選／取消、模式溫度風速、Matter QR 條件、Wi-Fi POST 與錯誤、來源與 SSID 安全、清除確認、離線停用、手機無水平溢位，以及沒有擺風文字／按鈕／請求。所有資源本機載入，無 JS 例外。

## 邊界

主機測試與模擬網頁不能代替實機冷氣、Apple 家庭及長時間穩定驗收。本次未刷機或操作實體紅外。

## 編譯與交付

ESPHome 2026.8.2／ESP-IDF 5.5.5，C3 4MB 編譯連結成功；應用 1,648,656 bytes。原始碼 C/C++/header 與編譯副本逐檔相同，舊擺風原始檔未殘留。生成空調端點僅 MultiSpeed／Auto，沒有 Rocking／RockSetting；生成 API 設定沒有學習按鈕或狀態。檢查 BIN 內含新版壓縮網頁、無舊學習文字，容量未超過 OTA 槽。

## 儲存相容性與回報保護

- 空調 PairingData 維持 4,420 bytes，主配對儲存鍵與候選識別不變；舊獨立擺風樣本不再讀取。不自動清除 Matter 家庭。
- 原廠遙控器接收不觸發紅外重送；本機狀態回報使用 LocalReportGuard 阻斷 Matter 同步回呼，避免回授造成自發控制。
- Matter SDK 鎖定 `davidvtwout/esp_matter 1.6.0~2`，component hash 為 `67c766e18e2fbc05707c8bf0e806cd35c40a361837c3c836bd7573730ec8ce60`。此保護依據該版 SDK 的同步回呼路徑；升級 SDK 時需重新驗證。
- UART 完整資料與格式由上層消費後才處理下一段，不增加無界佇列；未確認完成的紅外指令不自動重送。
- 仍需實機驗收快速連按、多段訊號品牌、手機紅外干擾恢復及長時間運作。Apple 家庭的顏色與舊介面快取由控制器決定。
