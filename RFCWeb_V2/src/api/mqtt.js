/**
 * api/mqtt.js —— MQTT / Home Assistant 配置
 */
import http from './http'

// GET /api/mqtt/config
export const getMqttConfig = () => http.get('/api/mqtt/config')

// POST /api/mqtt/config {mode, server, port, username, password}
export const saveMqttConfig = (data) => http.post('/api/mqtt/config', data)

// DELETE /api/mqtt/config —— 清除配置并断开
export const clearMqttConfig = () => http.delete('/api/mqtt/config')

// GET /api/mqtt/connect —— 自动发现并连接
export const connectMqtt = () => http.get('/api/mqtt/connect')

// GET /api/mqtt/status {connected, hasConfig}
export const mqttStatus = () => http.get('/api/mqtt/status')
