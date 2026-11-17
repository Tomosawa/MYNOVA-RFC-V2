<template>
  <v-dialog
    :model-value="modelValue"
    max-width="520"
    persistent
    @update:model-value="onClose"
  >
    <v-card>
      <v-card-title class="text-h6">
        <v-icon class="mr-2">sensors</v-icon>{{ t('capture.title') }}
      </v-card-title>

      <v-card-text>
        <!-- 配置区（未监听时显示） -->
        <template v-if="phase === 'idle'">
          <v-radio-group v-model="mode" inline density="compact" :label="t('capture.mode')">
            <v-radio :label="t('capture.band')" value="band"></v-radio>
            <v-radio :label="t('capture.fixed')" value="fixed"></v-radio>
          </v-radio-group>

          <v-select
            v-if="mode === 'band'"
            v-model="band"
            :items="bandItems"
            variant="outlined"
            density="compact"
          ></v-select>
          <v-select
            v-else
            v-model="freq"
            :items="frequencyOptions"
            :label="t('capture.freq')"
            variant="outlined"
            density="compact"
          ></v-select>

          <v-btn block color="primary" variant="tonal" size="large" prepend-icon="play_arrow"
                 :loading="starting" @click="start">
            {{ t('capture.start') }}
          </v-btn>
        </template>

        <!-- 监听中 -->
        <template v-else-if="phase === 'listening'">
          <div class="text-center py-4">
            <v-progress-circular indeterminate color="primary" size="48" width="4" class="mb-3"></v-progress-circular>
            <div class="text-body-1">{{ t('capture.listening') }}</div>
            <div v-if="scanFreq" class="text-caption text-medium-emphasis mt-1">
              {{ t('capture.scanAt') }}: {{ scanFreq }} MHz
            </div>
            <div class="text-caption text-medium-emphasis mt-2">{{ t('capture.pressHint') }}</div>
          </div>
          <v-btn block variant="outlined" color="error" @click="stop">
            {{ t('capture.stop') }}
          </v-btn>
        </template>

        <!-- 已捕获 -->
        <template v-else>
          <v-alert type="success" variant="tonal" density="compact" class="mb-3">
            {{ t('capture.captured') }}
          </v-alert>
          <v-list density="compact" class="mb-2">
            <v-list-item>
              <v-list-item-title>{{ t('capture.protocol') }}</v-list-item-title>
              <v-list-item-subtitle>{{ captured.protocolName || 'RAW' }}</v-list-item-subtitle>
            </v-list-item>
            <v-list-item>
              <v-list-item-title>{{ t('signals.frequency') }} / {{ t('capture.bitCount') }}</v-list-item-title>
              <v-list-item-subtitle>{{ captured.frequency }} MHz | {{ captured.bitCount }} bit</v-list-item-subtitle>
            </v-list-item>
            <v-list-item>
              <v-list-item-title>{{ t('signals.pulses') }} (S/L/Sync)</v-list-item-title>
              <v-list-item-subtitle>{{ captured.shortPulse }} / {{ captured.longPulse }} / {{ captured.syncPulse }} μs</v-list-item-subtitle>
            </v-list-item>
            <v-list-item>
              <v-list-item-title>{{ t('signals.dataHex') }}</v-list-item-title>
              <v-list-item-subtitle class="mono-hex">{{ captured.dataHex || '--' }}</v-list-item-subtitle>
            </v-list-item>
          </v-list>

          <v-text-field
            v-model="name"
            :label="t('capture.saveAs')"
            :rules="[v => (utf8Bytes(v) > 0 && utf8Bytes(v) <= 31) || t('signals.nameRule')]"
            variant="outlined"
            density="compact"
            :placeholder="t('signals.namePh')"
          ></v-text-field>

          <v-btn block color="primary" variant="tonal" class="mb-2" :loading="saving"
                 :disabled="!validName" @click="save">
            {{ t('capture.save') }}
          </v-btn>
          <v-btn block variant="outlined" @click="start">{{ t('capture.discardResume') }}</v-btn>
        </template>
      </v-card-text>

      <v-divider></v-divider>
      <v-card-actions>
        <v-spacer></v-spacer>
        <v-btn variant="plain" @click="onClose(false)">{{ t('common.close') }}</v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>

<script setup>
import { computed, onUnmounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { startRfRx, rfRxStatus, stopRfRx, saveRfCapture } from '@/api/rf'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
})

const emit = defineEmits(['update:modelValue', 'saved'])

const { t } = useI18n()
const app = useAppStore()

const phase = ref('idle')          // idle | listening | captured
const mode = ref('band')
const band = ref('high')
const freq = ref(433.92)
const starting = ref(false)
const saving = ref(false)
const scanFreq = ref(null)
const captured = ref(null)
const name = ref('')

let pollTimer = null

// CC1101 预设频点（与固件一致）
const frequencyOptions = [310, 315, 330, 350, 390, 418, 430.5, 431.5, 432.8, 433.92]
const bandItems = computed(() => [
  { title: t('capture.bandHigh'), value: 'high' },
  { title: t('capture.bandLow'), value: 'low' },
])

const utf8Bytes = (str) => new TextEncoder().encode(str || '').length
const validName = computed(() => {
  const n = utf8Bytes(name.value)
  return n > 0 && n <= 31
})

function clearPoll () {
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null }
}

async function start () {
  starting.value = true
  clearPoll()
  try {
    const body = mode.value === 'fixed' ? { frequency: freq.value } : { band: band.value }
    await startRfRx(body)
    phase.value = 'listening'
    scanFreq.value = null
    captured.value = null
    // 1s 轮询捕获结果（静默：无信号/超时由对话框自身展示）
    pollTimer = setInterval(async () => {
      try {
        const st = await rfRxStatus({ _silent: true })
        if (st.receiving) scanFreq.value = st.scanFrequency
        if (st.hasSignal) {
          clearPoll()
          scanFreq.value = null
          captured.value = st
          phase.value = 'captured'
        } else if (!st.receiving && !st.hasSignal && !st.owned) {
          // 会话被设备端接管，回到待机
          clearPoll()
          phase.value = 'idle'
        }
      } catch (e) {
        /* 单次轮询失败忽略，下次重试 */
      }
    }, 1000)
  } catch (e) {
    // busy / CC1101 not ready —— 拦截器已提示
  } finally {
    starting.value = false
  }
}

async function stop () {
  clearPoll()
  phase.value = 'idle'
  try { await stopRfRx({ _silent: true }) } catch (e) { /* 忽略 */ }
}

async function save () {
  if (!validName.value) return
  saving.value = true
  try {
    const data = await saveRfCapture(name.value)
    app.notify(t('capture.saved', { index: data.index }))
    emit('saved')
    emit('update:modelValue', false)
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    saving.value = false
  }
}

function onClose (open) {
  if (!open) {
    // 关闭时若仍在监听则停止（已捕获草稿由固件保留到下次会话）
    if (phase.value === 'listening') stop()
    clearPoll()
    phase.value = 'idle'
    captured.value = null
  }
  emit('update:modelValue', open)
}

onUnmounted(clearPoll)
</script>

<style scoped>
.mono-hex {
  font-family: monospace;
  word-break: break-all;
}
</style>
