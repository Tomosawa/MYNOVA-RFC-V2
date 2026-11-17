// Plugins
import Components from 'unplugin-vue-components/vite'
import Vue from '@vitejs/plugin-vue'
import Vuetify, { transformAssetUrls } from 'vite-plugin-vuetify'

// Utilities
import { defineConfig } from 'vite'
import { fileURLToPath, URL } from 'node:url'
import { existsSync, readdirSync, statSync, unlinkSync } from 'node:fs'
import { resolve } from 'node:path'

// LittleFS 体积优化：仅保留 woff2 图标字体
// （material-design-icons-iconfont 会输出 woff2/eot/woff/ttf 四种格式共约 777KB，
//   ESP32 Web 场景下现代浏览器只请求 woff2，其余直接从 dist 删除）
function trimFontFormats () {
  return {
    name: 'trim-font-formats',
    closeBundle () {
      const dist = fileURLToPath(new URL('./dist', import.meta.url))
      if (!existsSync(dist)) return
      let saved = 0
      for (const f of readdirSync(dist)) {
        if (/\.(eot|woff|ttf)$/.test(f)) {
          const p = resolve(dist, f)
          saved += statSync(p).size
          unlinkSync(p)
        }
      }
      // 汇总 dist 剩余体积（LittleFS 分区硬预算 0x1D0000 = 1.8125 MiB）
      let total = 0
      for (const f of readdirSync(dist)) total += statSync(resolve(dist, f)).size
      console.log(`[trim-font-formats] removed ${(saved / 1024).toFixed(0)} KB, dist total ${(total / 1024).toFixed(0)} KB (${(total / 1024 / 1024).toFixed(2)} MiB)`)
    },
  }
}

// https://vitejs.dev/config/
export default defineConfig({
  base: '/',
  plugins: [
    Vue({
      template: { transformAssetUrls }
    }),
    // https://github.com/vuetifyjs/vuetify-loader/tree/master/packages/vite-plugin#readme
    Vuetify(),
    Components(),
    trimFontFormats(),
  ],
  define: { 'process.env': {} },
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url))
    },
    extensions: [
      '.js',
      '.json',
      '.jsx',
      '.mjs',
      '.ts',
      '.tsx',
      '.vue',
    ],
  },
  server: {
    host:'0.0.0.0',
    port: 80,
    historyApiFallback: true,
    proxy: {
      '/api/': {
        target: 'http://192.168.4.1', //目标设备域名(热点模式=ESP32默认网段)
        changeOrigin: true, //需要代理跨域
        // 固件端点本身就带 /api/ 前缀，无需路径重写
      },
    },
  },
	build: {
	    rollupOptions: {
	      output: {
	        // 用于缩短静态资源的文件名
	        entryFileNames: '[hash].js',
	        chunkFileNames: '[hash].js',
	        assetFileNames: '[hash].[ext]',
	        // 使用更简洁的哈希长度
	        // entryFileNames: '[name].[hash:8].js',
	        // chunkFileNames: '[name].[hash:8].js',
	        // assetFileNames: '[name].[hash:8].[ext]',
	      },
	    },
	  },
})
