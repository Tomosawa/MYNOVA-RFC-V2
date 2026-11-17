<template>
  <v-card elevation="2">
    <v-card-title class="text-subtitle-1">
      <v-icon class="mr-2">build</v-icon>{{ t('settings.maintenance') }}
    </v-card-title>

    <v-card-text>
      <!-- 日志级别 -->
      <v-select
        v-model="logLevel"
        :items="logItems"
        :label="t('settings.logLevel')"
        variant="outlined"
        density="compact"
        hide-details
        class="mb-4"
        @update:model-value="onLogLevelChange"
      ></v-select>

      <!-- 重启 / 恢复出厂 -->
      <v-row dense>
        <v-col cols="6">
          <v-btn block color="error" variant="tonal" prepend-icon="restart_alt" :disabled="busy" @click="rebootDialog = true">
            {{ t('settings.reboot') }}
          </v-btn>
        </v-col>
        <v-col cols="6">
          <v-btn block color="error" variant="outlined" prepend-icon="restore" :disabled="busy" @click="resetDialog = true">
            {{ t('settings.factoryReset') }}
          </v-btn>
        </v-col>
      </v-row>

      <!-- OTA -->
      <v-divider class="my-4"></v-divider>
      <div class="text-subtitle-2 mb-2">
        <v-icon class="mr-1" size="18">system_update_alt</v-icon>{{ t('settings.ota') }}
      </div>
      <template v-if="otaInfo">
        <div class="text-caption text-medium-emphasis mb-2">
          {{ t('settings.otaCurrent') }}: V{{ otaInfo.version }}
          · {{ t('settings.otaPartition') }}: {{ otaInfo.partition }}
          · {{ t('settings.otaFree') }}: {{ freeText }}
        </div>
      </template>

      <input ref="fileInput" type="file" accept=".bin" class="d-none" @change="onFilePicked" />
      <v-btn block color="teal" variant="tonal" prepend-icon="upload_file"
             :disabled="busy || !otaInfo" @click="fileInput.click()">
        {{ fileName || t('settings.otaSelect') }}
      </v-btn>
      <v-btn v-if="file" block color="teal" variant="elevated" class="mt-2"
             :loading="uploading" :disabled="busy" @click="startUpload">
        {{ uploading ? t('settings.otaUploading', { p: progress }) : t('settings.otaUpload') }}
      </v-btn>

      <v-progress-linear
        v-if="uploading || otaStage"
        :model-value="progress"
        color="teal"
        height="10"
        rounded
        class="mt-3"
      ></v-progress-linear>
      <v-alert v-if="otaStage === 'rebooting'" type="info" variant="tonal" density="compact" class="mt-3">
        {{ t('settings.otaRebooting') }}
      </v-alert>
    </v-card-text>

    <!-- 重启确认：跳转仪表盘统一等待上线 -->
    <ConfirmDialog
      v-model="rebootDialog"
      :title="t('settings.rebootTitle')"
      :message="t('settings.rebootMsg')"
      :confirm-text="t('settings.reboot')"
      @confirm="doReboot"
    ></ConfirmDialog>

    <!-- 恢复出厂：输入 YES 确认（对齐设备端 FactoryResetPage） -->
    <v-dialog v-model="resetDialog" max-width="420" persistent>
      <v-card>
        <v-card-title class="text-h6">
          <v-icon color="error" class="mr-2">warning</v-icon>{{ t('settings.factoryReset') }}
        </v-card-title>
        <v-card-text>
          <v-alert type="error" variant="tonal" density="compact" class="mb-3">
            {{ t('settings.factoryResetWarn') }}
          </v-alert>
          <v-text-field
            v-model="resetConfirm"
            variant="outlined"
            density="compact"
            :placeholder="t('settings.factoryResetType')"
          ></v-text-field>
        </v-card-text>
        <v-card-actions>
          <v-spacer></v-spacer>
          <v-btn variant="text" @click="resetDialog = false">{{ t('common.cancel') }}</v-btn>
          <v-btn color="error" variant="flat"
                 :disabled="resetConfirm !== 'YES'" :loading="resetting"
                 @click="doFactoryReset">{{ t('settings.factoryResetBtn') }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </v-card>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import { useAppStore } from '@/stores/app'
import { getLogLevel, reboot, factoryReset, getOtaInfo, getStatus } from '@/api/device'
import ConfirmDialog from '@/components/ConfirmDialog.vue'

const { t } = useI18n()
const app = useAppStore()
const router = useRouter()

// ── 日志级别 ──
const logLevel = ref(3)
const logItems = computed(() => [0, 1, 2, 3, 4].map(v => ({
  title: `${v} · ${t(`settings.log${v}`)}`, value: v,
})))

onMounted(async () => {
  try {
    const data = await getLogLevel({ _silent: true })
    logLevel.value = data.level ?? 3
  } catch (e) { /* 忽略 */ }
  try {
    otaInfo.value = await getOtaInfo({ _silent: true })
  } catch (e) { /* 旧固件无此端点 */ }
})

async function onLogLevelChange (v) {
  try {
    await getLogLevel(v)
    app.notify(t('settings.logSaved'))
  } catch (e) {
    /* 拦截器已提示 */
  }
}

// ── 重启（确认后跳仪表盘统一等待上线） ──
const rebootDialog = ref(false)
const busy = computed(() => uploading.value || resetting.value)

async function doReboot () {
  rebootDialog.value = false
  try { await reboot({ _silent: true }) } catch (e) { /* 忽略 */ }
  app.notify(t('common.rebooting'))
  router.push('/')
}

// ── 恢复出厂 ──
const resetDialog = ref(false)
const resetConfirm = ref('')
const resetting = ref(false)

async function doFactoryReset () {
  resetting.value = true
  try {
    await factoryReset({ _silent: true })
  } catch (e) {
    /* 设备清空NVS后断开连接属正常，同样按重启处理 */
  }
  resetting.value = false
  resetDialog.value = false
  app.notify(t('settings.factoryResetDone'))
  setTimeout(() => router.push('/'), 800)
}

// ── OTA 上传（XHR 进度条） ──
const otaInfo = ref(null)
const fileInput = ref(null)
const file = ref(null)
const fileName = ref('')
const uploading = ref(false)
const progress = ref(0)
const otaStage = ref('')     // '' | 'rebooting' | 'done' | 'failed'

const freeText = computed(() => {
  const bytes = otaInfo.value ? otaInfo.value.freeSketchSpace : 0
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`
})

function onFilePicked (e) {
  const f = e.target.files && e.target.files[0]
  if (!f) return
  if (!f.name.toLowerCase().endsWith('.bin')) {
    app.notify(t('settings.otaFail'), 'error')
    return
  }
  file.value = f
  fileName.value = f.name
  otaStage.value = ''
}

function startUpload () {
  if (!file.value) return
  uploading.value = true
  progress.value = 0
  otaStage.value = ''

  const xhr = new XMLHttpRequest()
  const formData = new FormData()
  formData.append('firmware', file.value)

  xhr.open('POST', '/api/ota/upload')
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) progress.value = Math.round((e.loaded / e.total) * 100)
  }
  xhr.onload = async () => {
    uploading.value = false
    if (xhr.status === 200) {
      progress.value = 100
      otaStage.value = 'rebooting'
      await waitOnline()
    } else {
      otaStage.value = 'failed'
      app.notify(t('settings.otaFail'), 'error')
    }
    file.value = null
    fileName.value = ''
  }
  xhr.onerror = () => {
    uploading.value = false
    otaStage.value = 'failed'
    app.notify(t('settings.otaFail'), 'error')
  }
  xhr.send(formData)
}

const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms))

// 上传完成 → 轮询等待设备重启上线（最长 120s）
async function waitOnline () {
  await sleep(2000)
  const start = Date.now()
  while (Date.now() - start < 120000) {
    try {
      await getStatus({ _silent: true })
      otaStage.value = 'done'
      app.notify(t('settings.otaDone'))
      // 刷新 OTA 信息（新固件版本号）
      try { otaInfo.value = await getOtaInfo({ _silent: true }) } catch (e) { /* 忽略 */ }
      return
    } catch (e) {
      await sleep(3000)
    }
  }
  otaStage.value = 'failed'
  app.notify(t('common.waitOnlineTimeout'), 'error')
}
</script>
