/**
 * plugins/i18n.js
 *
 * 双语语言包：chs.js（中文）/ en.js（英文），App bar 下拉切换，localStorage 持久
 */
import { createI18n } from 'vue-i18n'
import chs from '@/locales/chs'
import en from '@/locales/en'

// 首次访问跟随浏览器语言（中文环境默认中文，其余默认英文）
const stored = localStorage.getItem('locale')
const fallback = (navigator.language || 'zh').toLowerCase().startsWith('zh') ? 'chs' : 'en'

const i18n = createI18n({
  legacy: false,              // Composition API 模式
  locale: stored || fallback,
  fallbackLocale: 'chs',
  messages: { chs, en },
})

export default i18n
