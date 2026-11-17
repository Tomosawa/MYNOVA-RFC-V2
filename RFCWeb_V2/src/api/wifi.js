/**
 * api/wifi.js —— WiFi STA / AP 热点配置
 */
import http from './http'

// GET /api/wifiscan —— 异步扫描，scanning=true 时需轮询
export const scanWifi = (opts) => http.get('/api/wifiscan', opts)

// POST /api/wifisave {SSID, PWD}
export const saveWifi = (SSID, PWD) => http.post('/api/wifisave', { SSID, PWD })

// DELETE /api/wificlear —— 清空WiFi配置并断开
export const clearWifi = () => http.delete('/api/wificlear')

// GET /api/wificonnect —— 用已存配置异步连接
export const connectWifi = () => http.get('/api/wificonnect')

// POST /api/wifienable {WIFI_ENABLE}
export const setWifiEnable = (enable) => http.post('/api/wifienable', { WIFI_ENABLE: enable })

// GET /api/apwifiinfo —— STA/AP 当前配置与实时连接状态
export const getApWifiInfo = (opts) => http.get('/api/apwifiinfo', opts)

// POST /api/apsave {AP_SSID, AP_PWD}
export const saveAp = (AP_SSID, AP_PWD) => http.post('/api/apsave', { AP_SSID, AP_PWD })

// POST /api/apenable {AP_ENABLE}
export const setApEnable = (enable) => http.post('/api/apenable', { AP_ENABLE: enable })
