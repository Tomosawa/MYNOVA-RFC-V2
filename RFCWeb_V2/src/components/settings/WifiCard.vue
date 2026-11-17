<template>
  <v-card elevation="2" class="mb-4">
    <v-card-title class="text-subtitle-1">
      <v-icon class="mr-2">wifi</v-icon>{{ t('settings.wifi') }}
    </v-card-title>

    <v-card-text>
      <v-form v-model="formValid" @submit.prevent="saveWifiForm">
        <v-combobox
          v-model="stSsid"
          :items="scanItems"
          item-title="SSID"
          item-value="SSID"
          :label="t('settings.wifiSsid')"
          :placeholder="t('settings.wifiScanPh')"
          variant="outlined"
          density="compact"
          :rules="[v => !!v || '']"
          prepend-inner-icon="wifi"
          class="mb-3"
        ></v-combobox>

        <v-text-field
          v-model="stPassword"
          :type="showPwd ? 'text' : 'password'"
          :label="t('settings.wifiPwd')"
          variant="outlined"
          density="compact"
          :rules="[v => !!v || '']"
          prepend-inner-icon="lock"
          :append-inner-icon="showPwd ? 'visibility_off' : 'visibility'"
          @click:append-inner="showPwd = !showPwd"
          class="mb-3"
        ></v-text-field>

        <v-btn block color="success" variant="elevated" size="large" type="submit"
               :disabled="!formValid" :loading="loading">
          {{ t('settings.wifiSave') }}
        </v-btn>
        <v-btn block color="error" variant="text" class="mt-2" size="large" @click="clearDialog = true">
          {{ t('settings.wifiClear') }}
        </v-btn>
      </v-form>

      <v-switch
        v-model="stEnable"
        :label="t('settings.wifiEnable')"
        color="success"
        inset
        density="compact"
        class="mt-2"
        @update:model-value="onEnableChanged"
      ></v-switch>

      <!-- 连接状态反馈 -->
      <v-alert v-if="connState === 'connecting'" type="info" variant="tonal" density="compact" class="mt-3">
        {{ t('settings.wifiConnecting') }}
      </v-alert>
      <v-alert v-else-if="connState === 'connected'" type="success" variant="tonal" density="compact" class="mt-3">
        {{ t('settings.wifiConnectedAs', { ip: connIp }) }}
      </v-alert>
      <v-alert v-else-if="connState === 'failed'" type="error" variant="tonal" density="compact" class="mt-3">
        {{ t('settings.wifiConnectFail') }}
      </v-alert>
    </v-card-text>

    <!-- 保存后询问立即连接 -->
    <v-dialog v-model="askConnect" persistent width="auto">
      <v-card>
        <v-card-title class="text-h6">{{ t('settings.wifiSaveAskTitle') }}</v-card-title>
        <v-card-text>{{ t('settings.wifiSaveAskMsg') }}</v-card-text>
        <v-card-actions>
          <v-spacer></v-spacer>
          <v-btn variant="text" @click="askConnect = false">{{ t('common.no') }}</v-btn>
          <v-btn color="success" variant="text" @click="doConnect">{{ t('settings.wifiConnectNow') }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- 清除配置确认 -->
    <ConfirmDialog
      v-model="clearDialog"
      :title="t('settings.wifiClearTitle')"
      :message="t('settings.wifiClearMsg')"
      :confirm-text="t('common.confirm')"
      @confirm="doClear"
    ></ConfirmDialog>
  </v-card>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { scanWifi, saveWifi, clearWifi, connectWifi, setWifiEnable, getApWifiInfo } from '@/api/wifi'
import ConfirmDialog from '@/components/ConfirmDialog.vue'

const { t } = useI18n()
const app = useAppStore()

const formValid = ref(false)
const loading = ref(false)
const showPwd = ref(false)
const stSsid = ref('')
const stPassword = ref('')
const stEnable = ref(false)
const scanItems = ref([])

const askConnect = ref(false)
const clearDialog = ref(false)

const connState = ref('')     // '' | connecting | connected | failed
const connIp = ref('')
let connTimer = null

// 扫描轮询（1s 递归，与固件异步扫描约定一致）
let scanTimer = null
function pollScan () {
  scanWifi({ _silent: true }).then(data => {
    if (data.scanning) {
      scanTimer = setTimeout(pollScan, 1000)
    } else {
      scanItems.value = data.WIFI_SCAN || []
    }
  }).catch(() => { /* 忽略 */ })
}

async function loadInfo () {
  try {
    const info = await getApWifiInfo({ _silent: true })
    stSsid.value = info.WIFI_SSID || ''
    stPassword.value = info.WIFI_PASSWORD || ''
    stEnable.value = !!info.WIFI_ENABLE
    if (info.WIFI_CONNECTED) {
      connState.value = 'connected'
      connIp.value = info.WIFI_IP || ''
    }
  } catch (e) { /* 忽略 */ }
}

onMounted(() => {
  loadInfo()
  pollScan()
})

onBeforeUnmount(() => {
  if (scanTimer) clearTimeout(scanTimer)
  if (connTimer) clearInterval(connTimer)
})

function saveWifiForm () { submitWifi() }

async function submitWifi () {
  if (!formValid.value) return
  loading.value = true
  const ssid = typeof stSsid.value === 'object' ? stSsid.value.SSID : stSsid.value
  try {
    await saveWifi(ssid, stPassword.value)
    loadInfo()
    askConnect.value = true
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    loading.value = false
  }
}

async function doConnect () {
  askConnect.value = false
  connState.value = 'connecting'
  try {
    const data = await connectWifi({ _silent: true })
    if (data.result === 'failed') {
      connState.value = 'failed'
      app.notify(data.message || t('settings.wifiConnectFail'), 'error')
      return
    }
    // 轮询连接结果（最长 20s）
    connTimer = setInterval(async () => {
      try {
        const info = await getApWifiInfo({ _silent: true })
        if (info.WIFI_CONNECTED) {
          clearInterval(connTimer)
          connTimer = null
          connState.value = 'connected'
          connIp.value = info.WIFI_IP || ''
        }
      } catch (e) { /* 继续等待 */ }
    }, 2000)
    setTimeout(() => {
      if (connTimer) {
        clearInterval(connTimer)
        connTimer = null
        getApWifiInfo({ _silent: true }).then(info => {
          if (info.WIFI_CONNECTED) {
            connState.value = 'connected'
            connIp.value = info.WIFI_IP || ''
          } else {
            connState.value = 'failed'
          }
        }).catch(() => { connState.value = 'failed' })
      }
    }, 20000)
  } catch (e) {
    connState.value = 'failed'
  }
}

async function doClear () {
  try {
    await clearWifi()
    clearDialog.value = false
    stSsid.value = ''
    stPassword.value = ''
    stEnable.value = false
    connState.value = ''
    app.notify(t('common.success'))
  } catch (e) {
    /* 拦截器已提示 */
  }
}

async function onEnableChanged (v) {
  try {
    await setWifiEnable(v)
  } catch (e) {
    stEnable.value = !v   // 失败回滚
  }
}
</script>
