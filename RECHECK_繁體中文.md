# v1.0.33 再次檢查結果

本次從已交付 v1.0.32 重新檢查原始碼、鎖定 ESPHome／Matter SDK、網頁實作、儲存相容性與交付映像；原 v1.0.32 保留供比較。

## 確認並修正

| 問題 | 重現與處理 |
|---|---|
| UART 多段積壓遺失前段 | 真實 bc7215.cpp + bc7215ac.cpp 同時輸入 40／48 bit 兩段，舊版只有最後 48 bit、sampleCount=1。改成完整資料／格式交由上層消費後才處理下一段；不增加無界佇列。 |
| 正常連網時更換 Wi-Fi 未生效 | ESPHome save_wifi_sta 在已連線／正在連線狀態不啟動新連線。Portal 改成 HTTP 回覆後延遲使用上游重連生命週期；AP 分支保留 AP 並避免正常 COOLDOWN 路徑重複啟動。 |
| 網頁風速與裝置狀態不符 | select 保持焦點會略過 SSE 回報，失敗或修正後仍保留錯誤值。移除這個阻擋，瀏覽器重現舊版失敗、新版通過。 |
| Matter 暖氣被拒絕 | 鎖定 SDK Thermostat 預設 ControlSequence=CoolingOnly，直接拒絕 Heat。產生器與升級開機回報都改為 CoolingAndHeating=4，先於 SystemMode 回報；保留非揮發與家庭資料。 |
| QR 提前隱藏或失敗後不能重試 | SDK AddNOC 尚未 CommissioningComplete 就增加 FabricCount。顯示數排除 pending 新紀錄，手動視窗旗標保留到視窗真正關閉，涵蓋加入失敗回滾後再次掃碼。 |
| 無效溫度仍保留 Matter 舊值 | YAML 收到 NaN／無限值／越界值時，使用 SDK nullable INT16 的 -32768 回報未知；有效值恢復時正常回報。此項不宣稱能偵測沒有發布新讀值的硬體故障。 |

## 已核對

- 擺風控制、學習、狀態機、儲存載入、同步、網頁文字／按鈕與 Matter Rocking 均無啟用路徑；FanControl 三速與 Auto 保留。
- PairingData 4,420 bytes、主配對鍵與候選識別維持。取消配對可還原；發送中排入調溫／關機不互相覆蓋；未知欄位保留前值並標記未完整同步。
- 原廠遙控接收不觸發紅外重送；狀態回報保留 LocalReportGuard，避免 Matter 回授造成自發控制。
- 接線 RX2/TX3/MOD0/BUSY1、19200 8N2；SHT40 SDA4/SCL5；RGB8。燈號與配對流程維持現行規則。
- 配網輸入限制、來源檢查、POST 密碼、錯誤回覆、離線停用與刪除家庭確認保留。
- 舊 v1.0.32 ZIP 與來源逐檔一致，SHA256、factory/OTA 應用區相同、分割區容量與無重疊均通過；新版交付亦採同樣檢查。

## 實機待驗證

本次未刷入、未發射紅外或清除已配對家庭。原先一般調溫也曾有完成回覆逾時，這次修正不能據此宣稱該現象全部解決。仍需在真實冷氣上驗證快速連按、多段品牌、手機干擾恢復及長時間運作。Apple 家庭顏色與舊擺葉介面快取仍由控制器決定。
