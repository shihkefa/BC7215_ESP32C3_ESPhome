# v1.0.33 韌體下載

| 檔案 | 用途 |
|---|---|
| [Factory BIN](BC7215A_v1.0.33_Matter_factory_需配網.bin?raw=true) | 新板首次 USB 安裝，寫入位址 **0x0** |
| [OTA BIN](BC7215A_v1.0.33_Matter_ota_需配網.bin?raw=true) | 已安裝本專案 Matter 韌體時，透過 ESPHome OTA 更新 |
| [SHA256 校驗碼](SHA256SUMS.txt) | 下載後核對檔案完整性 |

不要把 OTA 映像寫到 0x0。一般更新不需擦除 Flash／NVS；升級不會主動清除 Matter 或空調配對。
BIN 未包含使用者的 Wi-Fi 憑證，需透過 AP 配網：`bc7215a-c3-xxxxxx`、密碼 `12345678`、網址 http://192.168.4.1/ 。

詳細步驟見 [完整使用說明](../README.md) 與 [燒錄說明](../BIN_燒錄說明.md)。
