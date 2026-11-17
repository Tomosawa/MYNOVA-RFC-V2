<template>
  <v-container fluid class="pb-16">
    <!-- 离线横幅 -->
    <v-alert
      v-if="!device.online"
      type="error"
      variant="tonal"
      density="compact"
      class="mb-4"
    >{{ t('common.offline') }}</v-alert>

    <template v-if="s">
      <v-row dense>
        <!-- 设备信息 -->
        <v-col cols="12" sm="6" md="4">
          <v-card elevation="2" height="100%">
            <v-card-title class="text-subtitle-1">
              <v-icon class="mr-2">info</v-icon>{{ t('dashboard.deviceInfo') }}
            </v-card-title>
            <v-card-text>
              <v-row dense>
                <v-col cols="6">
                  <div class="text-caption text-medium-emphasis">{{ t('dashboard.model') }}</div>
                  <div class="text-body-2">{{ s.model }}</div>
                </v-col>
                <v-col cols="6">
                  <div class="text-caption text-medium-emphasis">{{ t('dashboard.firmware') }}</div>
                  <div class="text-body-2">V{{ s.version }}</div>
                </v-col>
                <v-col cols="6">
                  <div class="text-caption text-medium-emphasis">{{ t('dashboard.buildDate') }}</div>
                  <div class="text-body-2">{{ s.buildDate }}</div>
                </v-col>
                <v-col cols="6">
                  <div class="text-caption text-medium-emphasis">{{ t('dashboard.serial') }}</div>
                  <div class="text-body-2 mono">{{ s.serial }}</div>
                </v-col>
                <v-col cols="12">
                  <div class="text-caption text-medium-emphasis">{{ t('dashboard.uptime') }}</div>
                  <div class="text-body-2">{{ uptimeText }}</div>
                </v-col>
              </v-row>
            </v-card-text>
          </v-card>
        </v-col>

        <!-- 电池 -->
        <v-col cols="6" sm="3" md="2">
          <v-card elevation="2" height="100%">
            <v-card-title class="text-subtitle-1">
              <v-icon class="mr-2">battery_charging_full</v-icon>{{ t('dashboard.battery') }}
            </v-card-title>
            <v-card-text class="text-center">
              <div class="text-h4">{{ Math.round(s.batteryPercent) }}%</div>
              <div class="text-caption text-medium-emphasis mb-2">{{ s.batteryVoltage.toFixed(2) }} V</div>
              <v-progress-linear
                :model-value="s.batteryPercent"
                :color="device.batteryColor"
                height="8"
                rounded
              ></v-progress-linear>
            </v-card-text>
          </v-card>
        </v-col>

        <!-- 内存 -->
        <v-col cols="6" sm="3" md="2">
          <v-card elevation="2" height="100%">
            <v-card-title class="text-subtitle-1">
              <v-icon class="mr-2">memory</v-icon>{{ t('dashboard.memory') }}
            </v-card-title>
            <v-card-text>
              <div class="text-caption text-medium-emphasis">{{ t('dashboard.ramFree') }}</div>
              <div class="text-body-2 mb-1">{{ kbText(s.heapFree) }}</div>
              <div class="text-caption text-medium-emphasis">{{ t('dashboard.psramFree') }}</div>
              <div class="text-body-2">{{ kbText(s.psramFree) }}</div>
            </v-card-text>
          </v-card>
        </v-col>

        <!-- 网络 -->
        <v-col cols="12" sm="6" md="4">
          <v-card elevation="2" height="100%">
            <v-card-title class="text-subtitle-1">
              <v-icon class="mr-2">wifi</v-icon>{{ t('dashboard.network') }}
            </v-card-title>
            <v-card-text>
              <template v-if="s.wifi.connected">
                <div class="text-body-2">{{ t('dashboard.wifiConnected', { ssid: s.wifi.ssid }) }}</div>
                <div class="text-caption text-medium-emphasis">IP {{ s.wifi.ip }} · RSSI {{ s.wifi.rssi }} dBm</div>
              </template>
              <div v-else-if="s.wifi.enabled" class="text-body-2">{{ t('dashboard.wifiDisconnected') }}</div>
              <div v-else class="text-body-2">{{ t('dashboard.wifiDisabled') }}</div>

              <v-divider class="my-2"></v-divider>
              <div class="text-body-2">
                <v-chip
                  :color="s.ap.enabled ? 'teal' : undefined"
                  :variant="s.ap.enabled ? 'tonal' : 'outlined'"
                  size="small" class="mr-2"
                >AP</v-chip>
                {{ s.ap.enabled ? t('dashboard.apEnabled', { ssid: s.ap.ssid }) : t('dashboard.apDisabled') }}
              </div>
              <v-divider class="my-2"></v-divider>
              <div class="text-body-2">
                <v-chip
                  :color="s.mqtt.connected ? 'success' : undefined"
                  :variant="s.mqtt.connected ? 'tonal' : 'outlined'"
                  size="small" class="mr-2"
                >MQTT</v-chip>
                {{ s.mqtt.connected ? t('dashboard.mqttConnected') : t('dashboard.mqttDisconnected') }}
              </div>
            </v-card-text>
          </v-card>
        </v-col>

        <!-- 数据统计 -->
        <v-col cols="12" md="8">
          <v-card elevation="2" height="100%">
            <v-card-title class="text-subtitle-1">
              <v-icon class="mr-2">storage</v-icon>{{ t('dashboard.dataStats') }}
            </v-card-title>
            <v-card-text>
              <v-row dense>
                <v-col cols="4">
                  <v-card variant="tonal" color="primary" class="stat-card" @click="$router.push('/data?tab=rf')">
                    <div class="text-h4">{{ s.rfCount }}</div>
                    <div class="text-caption">{{ t('dashboard.rfSignals') }}</div>
                  </v-card>
                </v-col>
                <v-col cols="4">
                  <v-card variant="tonal" color="orange" class="stat-card" @click="$router.push('/data?tab=ir')">
                    <div class="text-h4">{{ s.irCount }}</div>
                    <div class="text-caption">{{ t('dashboard.irSignals') }}</div>
                  </v-card>
                </v-col>
                <v-col cols="4">
                  <v-card variant="tonal" color="teal" class="stat-card" @click="$router.push('/remote?view=home')">
                    <div class="text-h4">{{ s.homeCount }}</div>
                    <div class="text-caption">{{ t('dashboard.homeProfiles') }}</div>
                  </v-card>
                </v-col>
              </v-row>
            </v-card-text>
          </v-card>
        </v-col>

        <!-- 快捷操作 -->
        <v-col cols="12" md="4">
          <v-card elevation="2" height="100%">
            <v-card-title class="text-subtitle-1">
              <v-icon class="mr-2">bolt</v-icon>{{ t('dashboard.quickActions') }}
            </v-card-title>
            <v-card-text>
              <v-btn block color="error" variant="tonal" prepend-icon="restart_alt" :disabled="waiting" @click="confirmReboot = true">
                {{ t('dashboard.reboot') }}
              </v-btn>
            </v-card-text>
          </v-card>
        </v-col>
      </v-row>
    </template>

    <!-- 未加载骨架 -->
    <v-row v-else-if="device.online === false && !s" dense>
      <v-col cols="12">
        <v-card variant="outlined" class="text-center pa-10">
          <v-icon size="64" color="grey">cloud_off</v-icon>
        </v-card>
      </v-col>
    </v-row>

    <!-- 重启确认 -->
    <ConfirmDialog
      v-model="confirmReboot"
      :title="t('dashboard.rebootConfirmTitle')"
      :message="t('dashboard.rebootConfirmMsg')"
      :confirm-text="t('dashboard.reboot')"
      :loading="waiting"
      @confirm="doReboot"
    ></ConfirmDialog>

    <!-- 重启等待遮罩 -->
    <v-dialog v-model="waiting" persistent fullscreen>
      <v-card color="teal" class="d-flex flex-column align-center justify-center" height="100%">
        <v-progress-circular indeterminate size="72" width="6" class="mb-6"></v-progress-circular>
        <div class="text-h6">{{ rebootStageText }}</div>
      </v-card>
    </v-dialog>
  </v-container>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { useDeviceStore } from '@/stores/device'
import { usePolling } from '@/composables/usePolling'
import { reboot, getStatus } from '@/api/device'
import ConfirmDialog from '@/components/ConfirmDialog.vue'

const { t } = useI18n()
const app = useAppStore()
const device = useDeviceStore()

const s = computed(() => device.status)
const confirmReboot = ref(false)
const waiting = ref(false)
const stage = ref('rebooting') // rebooting | waiting

const rebootStageText = computed(() =>
  stage.value === 'rebooting' ? t('common.rebooting') : t('common.waitingOnline'))

const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms))

// 轮询：在线 5s，离线降频 15s（页面隐藏自动暂停）
const polling = usePolling(
  () => device.refresh(),
  () => (device.online ? 5000 : 15000),
)

onMounted(() => {
  polling.runNow()
  polling.start()
})

// 运行时长格式化
const uptimeText = computed(() => {
  const sec = s.value ? s.value.uptimeSec : 0
  const d = Math.floor(sec / 86400)
  const h = Math.floor((sec % 86400) / 3600)
  const m = Math.floor((sec % 3600) / 60)
  const parts = []
  if (d) parts.push(`${d} ${t('dashboard.days')}`)
  if (h) parts.push(`${h} ${t('dashboard.hours')}`)
  parts.push(`${m} ${t('dashboard.minutes')}`)
  return parts.join(' ')
})

const kbText = (bytes) => (bytes >= 1024 * 1024)
  ? `${(bytes / 1024 / 1024).toFixed(1)} MB`
  : `${Math.round(bytes / 1024)} KB`

async function doReboot () {
  waiting.value = true
  stage.value = 'rebooting'
  try {
    await reboot({ _silent: true }).catch(() => {})
    await sleep(1500)
    stage.value = 'waiting'
    // 轮询等待设备重新上线（最长 120s）
    const start = Date.now()
    while (Date.now() - start < 120000) {
      try {
        await getStatus({ _silent: true })
        await device.refresh()
        app.notify(t('dashboard.rebootDone'))
        return
      } catch (e) {
        await sleep(3000)
      }
    }
    app.notify(t('common.waitOnlineTimeout'), 'error')
  } finally {
    waiting.value = false
    confirmReboot.value = false
  }
}
</script>

<style scoped>
.stat-card {
  text-align: center;
  padding: 18px 8px;
  cursor: pointer;
  transition: transform 0.2s ease;
}
.stat-card:hover {
  transform: translateY(-3px);
}
.mono {
  font-family: monospace;
}
</style>
