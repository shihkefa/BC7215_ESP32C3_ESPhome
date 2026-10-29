# v1.0.25 根因依據

本次以實際編譯所使用的 davidvtwout/esp_matter 元件為依據，非僅依函式名稱推定。

- `components/esp_matter/data_model/esp_matter_attribute_utils.cpp`：report 呼叫 update_or_report(..., false)。
- `components/esp_matter/data_model/esp_matter_data_model.cpp`：set_val 的 ATTRIBUTE_FLAG_WRITABLE 分支直接進入 set_val_via_write_attribute，沒有傳遞 call_callbacks。
- `components/esp_matter/data_model_provider/esp_matter_data_model_provider.cpp`：WriteAttribute 在成功時執行 POST_UPDATE，包括值未變的成功路徑。

本專案在 CHIP thread 上的本機 report 作用域阻斷同步回呼，不修改 SDK 套件、不使用延時忽略視窗，也不封鎖作用域外的真正遠端要求。若將來 SDK 改成非同步回呼，需要重新驗證此假設。

本次鎖定 davidvtwout/esp_matter `1.6.0~2`；component hash `67c766e18e2fbc05707c8bf0e806cd35c40a361837c3c836bd7573730ec8ce60`。
