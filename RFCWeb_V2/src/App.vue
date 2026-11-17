<template>
  <v-app>
    <v-main>
      <v-app-bar density="compact" color="teal">
        <v-app-bar-title>{{ pageTitle }}</v-app-bar-title>
        <template #append>
          <v-select
            v-model="localeSel"
            :items="localeItems"
            density="compact"
            variant="outlined"
            hide-details
            flat
            class="locale-select"
            aria-label="Language"
          ></v-select>
          <v-btn
            :icon="themeIcon"
            :title="themeTitle"
            @click="app.cycleTheme()"
          ></v-btn>
        </template>
      </v-app-bar>

      <router-view></router-view>

      <v-bottom-navigation v-model="tab" color="teal" grow>
        <v-btn value="/">
          <v-icon>dashboard</v-icon>
          <span>{{ t('nav.dashboard') }}</span>
        </v-btn>
        <v-btn value="/remote">
          <v-icon>radio_button_checked</v-icon>
          <span>{{ t('nav.remote') }}</span>
        </v-btn>
        <v-btn value="/data">
          <v-icon>storage</v-icon>
          <span>{{ t('nav.data') }}</span>
        </v-btn>
        <v-btn value="/settings">
          <v-icon>settings</v-icon>
          <span>{{ t('nav.settings') }}</span>
        </v-btn>
      </v-bottom-navigation>

      <!-- 全局通知（api/http.js 拦截器与各页面共用） -->
      <v-snackbar
        v-model="app.snackbar"
        :color="app.snackbarColor"
        :timeout="2500"
        location="top"
      >
        {{ app.snackbarText }}
      </v-snackbar>
    </v-main>
  </v-app>
</template>

<script setup>
import { computed, onUnmounted, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useTheme } from 'vuetify'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'

const route = useRoute()
const router = useRouter()
const app = useAppStore()
const { t, locale } = useI18n()
const theme = useTheme()

// ── 页面标题（随路由切换） ──
const pageTitle = computed(() => t(route.meta.titleKey || 'nav.dashboard'))

// ── 底栏导航：v-model 双向绑定到当前路径 ──
const tab = computed({
  get: () => route.path,
  set: (value) => router.push(value),
})

// ── 语言下拉选择器：chs.js / en.js 分包，localStorage 持久 ──
const localeItems = [
  { title: '中文', value: 'chs' },
  { title: 'English', value: 'en' },
]
if (!app.locale) app.setLocale(locale.value)   // 首次访问：初始化为 i18n 推断的浏览器语言
const localeSel = ref(app.locale || locale.value)
watch(localeSel, (v) => {
  app.setLocale(v)
  locale.value = v
})
watch(() => app.locale, (v) => { if (v && v !== localeSel.value) localeSel.value = v })

// ── 主题三态：light / dark / system（跟随系统） ──
const media = window.matchMedia('(prefers-color-scheme: dark)')

function applyTheme () {
  const dark = app.themeMode === 'dark' || (app.themeMode === 'system' && media.matches)
  theme.change(dark ? 'dark' : 'light')   // Vuetify 3.13+ API（theme.global.name 已弃用）
}
watch(() => app.themeMode, applyTheme, { immediate: true })

const onMediaChange = () => { if (app.themeMode === 'system') applyTheme() }
media.addEventListener('change', onMediaChange)
onUnmounted(() => media.removeEventListener('change', onMediaChange))

const themeIcon = computed(() =>
  app.themeMode === 'light' ? 'light_mode' : (app.themeMode === 'dark' ? 'dark_mode' : 'auto_mode'))
const themeTitle = computed(() =>
  app.themeMode === 'light' ? 'Light' : (app.themeMode === 'dark' ? 'Dark' : 'System'))
</script>

<style>
/* 语言下拉宽度约束（app bar 内） */
.locale-select {
  max-width: 112px;
  margin-top: 6px;
}
</style>
