<template>
  <v-dialog
    :model-value="modelValue"
    max-width="520"
    @update:model-value="$emit('update:modelValue', $event)"
  >
    <v-card :loading="loading">
      <v-card-title class="text-h6">
        {{ t('common.detail') }}
        <v-chip v-if="signal" :color="signal.kind === 'IR' ? 'orange' : 'primary'" size="small" class="ml-2">
          {{ signal.kind }} #{{ signal.index }}
        </v-chip>
      </v-card-title>

      <v-card-text v-if="signal">
        <v-list density="compact">
          <v-list-item>
            <v-list-item-title>{{ t('common.name') }}</v-list-item-title>
            <v-list-item-subtitle>{{ signal.name || t('common.unnamed') }}</v-list-item-subtitle>
          </v-list-item>
          <v-list-item v-if="signal.kind === 'RF'">
            <v-list-item-title>{{ t('signals.frequency') }}</v-list-item-title>
            <v-list-item-subtitle>{{ signal.frequency }} MHz</v-list-item-subtitle>
          </v-list-item>
          <v-list-item v-if="signal.kind === 'RF'">
            <v-list-item-title>{{ t('signals.bitCount') }}</v-list-item-title>
            <v-list-item-subtitle>{{ signal.bitCount }} bit</v-list-item-subtitle>
          </v-list-item>
          <v-list-item v-if="signal.kind === 'RF'">
            <v-list-item-title>{{ t('signals.pulses') }} (S/L/Sync)</v-list-item-title>
            <v-list-item-subtitle>{{ signal.shortPulse }} / {{ signal.longPulse }} / {{ signal.syncPulse }} μs</v-list-item-subtitle>
          </v-list-item>
          <v-list-item>
            <v-list-item-title>{{ t('signals.protocol') }}</v-list-item-title>
            <v-list-item-subtitle>{{ signal.protocolName || 'RAW' }}</v-list-item-subtitle>
          </v-list-item>
          <v-list-item v-if="signal.kind === 'RF'">
              <v-list-item-title>{{ t('signals.dataCol') }}</v-list-item-title>
              <v-list-item-subtitle class="mono-hex wrap-text">{{ signal.dataHex || '--' }}</v-list-item-subtitle>
            </v-list-item>
          <v-list-item v-if="signal.kind === 'IR'">
            <v-list-item-title>{{ t('signals.frames') }} / {{ t('signals.edges') }}</v-list-item-title>
            <v-list-item-subtitle>{{ signal.frameCount }} / {{ signal.totalEdges }}</v-list-item-subtitle>
          </v-list-item>

          <!-- IR v2 帧解码详情（每帧位宽/码值/地址/命令，旧数据无则为空） -->
          <template v-if="signal.kind === 'IR' && hasIrFrames">
            <v-list-subheader class="text-caption">{{ t('signals.frameDetail') }}</v-list-subheader>
            <v-list-item v-for="(bits, i) in signal.frameBits" :key="i" density="compact">
              <v-list-item-title class="text-body-2">#{{ i + 1 }} · {{ bits || '—' }} bit</v-list-item-title>
              <v-list-item-subtitle class="mono-hex">
                0x{{ signal.frameValues[i] || '?' }}
                <span v-if="signal.frameAddrs && signal.frameAddrs[i] && signal.frameAddrs[i] !== '0'"> · addr 0x{{ signal.frameAddrs[i] }}</span>
                <span v-if="signal.frameCmds && signal.frameCmds[i] && signal.frameCmds[i] !== '0'"> · cmd 0x{{ signal.frameCmds[i] }}</span>
              </v-list-item-subtitle>
            </v-list-item>
            <v-list-item v-if="signal.acState" density="compact">
              <v-list-item-title>{{ t('signals.acState') }}</v-list-item-title>
              <v-list-item-subtitle class="wrap-text">{{ signal.acState }}</v-list-item-subtitle>
            </v-list-item>
          </template>
        </v-list>
      </v-card-text>

      <v-card-text v-else-if="empty" class="text-center pa-6 text-grey">
        {{ t('signals.noData') }}
      </v-card-text>

      <v-divider></v-divider>
      <v-card-actions>
        <v-spacer></v-spacer>
        <v-btn variant="plain" @click="$emit('update:modelValue', false)">{{ t('common.close') }}</v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { getSignal } from '@/api/signals'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
  index: { type: Number, default: null },
})

defineEmits(['update:modelValue'])

const { t } = useI18n()
const signal = ref(null)
const empty = ref(false)
const loading = ref(false)

// IR v2 详情是否有效（旧数据 frameBits 全为 0 时视为无解码详情）
const hasIrFrames = computed(() =>
  signal.value && Array.isArray(signal.value.frameBits) && signal.value.frameBits.some(b => b > 0))

watch(() => props.modelValue, async (open) => {
  if (!open || props.index == null) return
  loading.value = true
  signal.value = null
  empty.value = false
  try {
    const data = await getSignal(props.index)
    if (data.result === 'EMPTY') {
      empty.value = true
    } else {
      signal.value = data
    }
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.mono-hex {
  font-family: monospace;
  word-break: break-all;
}
/* Vuetify 3 的 v-list-item-subtitle 用 -webkit-line-clamp:1 + display:-webkit-box 截断，
   必须覆盖截断三件套（display/line-clamp/overflow）才能换行，仅改 white-space 无效 */
.wrap-text {
  display: block !important;
  -webkit-line-clamp: unset !important;
  line-clamp: unset !important;
  -webkit-box-orient: initial !important;
  white-space: normal !important;
  overflow: visible !important;
  text-overflow: clip !important;
}
</style>
