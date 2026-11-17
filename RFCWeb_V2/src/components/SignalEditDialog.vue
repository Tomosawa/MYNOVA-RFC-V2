<template>
  <v-dialog
    :model-value="modelValue"
    max-width="600"
    persistent
    @update:model-value="$emit('update:modelValue', $event)"
  >
    <v-form ref="form" v-model="formValid" @submit.prevent="save">
      <v-card>
        <v-card-title>
          <span class="text-h5">{{ isIr ? t('signals.editIr') : (isEdit ? t('signals.editRf') : t('signals.addRf')) }}</span>
        </v-card-title>

        <v-card-text>
          <!-- 红外：仅改名 + 只读信息 -->
          <template v-if="isIr">
            <v-alert type="info" variant="tonal" density="compact" class="mb-3">
              {{ t('signals.irReadonlyHint') }}
            </v-alert>
            <v-row dense>
              <v-col cols="12">
                <v-text-field
                  v-model="form.name"
                  :label="t('signals.name')"
                  :rules="[rules.nameRequired, rules.nameBytes]"
                  :hint="`${nameByteLen}/31 ${t('common.bytes')}`"
                  persistent-hint
                  variant="outlined"
                  density="compact"
                  :placeholder="t('signals.namePh')"
                ></v-text-field>
              </v-col>
              <v-col cols="6">
                <v-text-field
                  :model-value="form.protocolName" :label="t('signals.protocol')" variant="outlined"
                  density="compact" hide-details readonly
                ></v-text-field>
              </v-col>
              <v-col cols="3">
                <v-text-field
                  :model-value="form.frameCount" :label="t('signals.frames')" variant="outlined"
                  density="compact" hide-details readonly
                ></v-text-field>
              </v-col>
              <v-col cols="3">
                <v-text-field
                  :model-value="form.totalEdges" :label="t('signals.edges')" variant="outlined"
                  density="compact" hide-details readonly
                ></v-text-field>
              </v-col>

              <!-- v2 帧解码详情（编辑时可见，旧数据无解码详情则隐藏） -->
              <v-col v-if="hasIrFrames" cols="12">
                <div class="text-caption text-medium-emphasis mb-1">{{ t('signals.frameDetail') }}</div>
                <div v-for="(bits, i) in irDetail.frameBits" :key="i" class="text-caption mono-hex mb-1">
                  #{{ i + 1 }} · {{ bits || '—' }} bit · 0x{{ irDetail.frameValues[i] || '?' }}
                  <span v-if="irDetail.frameAddrs && irDetail.frameAddrs[i] && irDetail.frameAddrs[i] !== '0'"> · addr 0x{{ irDetail.frameAddrs[i] }}</span>
                  <span v-if="irDetail.frameCmds && irDetail.frameCmds[i] && irDetail.frameCmds[i] !== '0'"> · cmd 0x{{ irDetail.frameCmds[i] }}</span>
                </div>
                <div v-if="irDetail.acState" class="text-caption">
                  {{ t('signals.acState') }}: {{ irDetail.acState }}
                </div>
              </v-col>
            </v-row>
          </template>

          <!-- 射频：完整编辑 -->
          <template v-else>
            <v-row dense>
              <v-col cols="12">
                <v-text-field
                  v-model="form.name"
                  :label="t('signals.name')"
                  :rules="[rules.nameRequired, rules.nameBytes]"
                  :hint="`${nameByteLen}/31 ${t('common.bytes')}`"
                  persistent-hint
                  variant="outlined"
                  density="compact"
                  :placeholder="t('signals.namePh')"
                ></v-text-field>
              </v-col>

              <v-col cols="6">
                <v-select
                  v-model="form.frequency"
                  :label="t('signals.frequency')"
                  :items="frequencyOptions"
                  :rules="[v => !!v || '']"
                  variant="outlined"
                  density="compact"
                ></v-select>
              </v-col>

              <v-col cols="6">
                <v-select
                  v-model="form.bitCount"
                  :label="t('signals.bitCount')"
                  :items="bitLengthOptions"
                  :rules="[v => !!v || '']"
                  variant="outlined"
                  density="compact"
                  @update:model-value="onBitLengthChange"
                ></v-select>
              </v-col>

              <v-col cols="4">
                <v-text-field
                  v-model.number="form.shortPulse"
                  :label="t('signals.shortPulse')"
                  type="number"
                  :rules="[rules.required, rules.pulseRange]"
                  variant="outlined"
                  density="compact"
                ></v-text-field>
              </v-col>

              <v-col cols="4">
                <v-text-field
                  v-model.number="form.longPulse"
                  :label="t('signals.longPulse')"
                  type="number"
                  :rules="[rules.required, rules.pulseRange]"
                  variant="outlined"
                  density="compact"
                ></v-text-field>
              </v-col>

              <v-col cols="4">
                <v-text-field
                  v-model.number="form.syncPulse"
                  :label="t('signals.syncPulse')"
                  type="number"
                  :rules="[rules.required, rules.syncRange]"
                  variant="outlined"
                  density="compact"
                ></v-text-field>
              </v-col>

              <v-col cols="12">
                <v-text-field
                  v-model="form.dataHex"
                  :label="t('signals.dataHex')"
                  :rules="[rules.required, rules.hexFormat, rules.hexLength]"
                  variant="outlined"
                  density="compact"
                  prefix="0x"
                  :hint="`max ${maxHexDigits} hex`"
                  persistent-hint
                  @input="onDataHexInput"
                ></v-text-field>
              </v-col>

              <v-col cols="12">
                <v-text-field
                  v-model="form.protocolName"
                  :label="t('signals.protocol')"
                  variant="outlined"
                  density="compact"
                  :placeholder="t('signals.protocolPh')"
                  hide-details
                ></v-text-field>
              </v-col>
            </v-row>
          </template>
        </v-card-text>

        <v-divider></v-divider>

        <v-card-actions>
          <v-spacer></v-spacer>
          <v-btn variant="plain" @click="$emit('update:modelValue', false)">{{ t('common.cancel') }}</v-btn>
          <v-btn
            color="primary"
            variant="tonal"
            type="submit"
            :disabled="!formValid"
            :loading="saving"
          >{{ t('common.save') }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-form>
  </v-dialog>
</template>

<script setup>
import { computed, reactive, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { addSignal, updateSignal, getSignal } from '@/api/signals'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
  // 编辑目标：null=新增；kind==='IR' 仅改名
  item: { type: Object, default: null },
})

const emit = defineEmits(['update:modelValue', 'saved'])

const { t } = useI18n()
const app = useAppStore()

const form = reactive({
  index: null, kind: 'RF', name: '',
  frequency: 433.92, bitCount: 24, shortPulse: 300, longPulse: 900, syncPulse: 9000,
  protocolName: '', dataHex: '',
  frameCount: 0, totalEdges: 0,
})
const formValid = ref(false)
const saving = ref(false)

const isIr = computed(() => form.kind === 'IR')
const isEdit = computed(() => form.index != null)

// CC1101 支持的10个频点（与固件一致）
const frequencyOptions = [310, 315, 330, 350, 390, 418, 430.5, 431.5, 432.8, 433.92]
const bitLengthOptions = [4, 8, 12, 16, 20, 24, 28, 32, 36, 40]

// 名称规则放宽：中文/字母/数字，UTF-8 字节数 ≤31（NVS 名称字段上限）
const utf8Bytes = (str) => new TextEncoder().encode(str || '').length
const nameByteLen = computed(() => utf8Bytes(form.name))

const maxHexDigits = computed(() => Math.min(Math.ceil(form.bitCount / 4), 64))

const rules = {
  required: v => (v !== '' && v !== null && v !== undefined) || ' ',
  pulseRange: v => (v >= 50 && v <= 15000) || '50-15000',
  syncRange: v => (v >= 0 && v <= 30000) || '0-30000',
  nameRequired: v => (v && v.trim().length > 0) || t('signals.nameRule'),
  nameBytes: v => utf8Bytes(v) <= 31 || t('signals.nameRule'),
  hexFormat: v => !v || /^[0-9A-Fa-f]*$/.test(v) || 'HEX',
  hexLength: v => !v || v.length <= maxHexDigits.value || `max ${maxHexDigits.value}`,
}

watch(() => props.modelValue, (open) => {
  if (!open) return
  const src = props.item || {}
  Object.assign(form, {
    index: src.index ?? null,
    kind: src.kind || 'RF',
    name: src.name || '',
    frequency: src.frequency ?? 433.92,
    bitCount: src.bitCount ?? 24,
    shortPulse: src.shortPulse ?? 300,
    longPulse: src.longPulse ?? 900,
    syncPulse: src.syncPulse ?? 9000,
    protocolName: src.protocolName || '',
    dataHex: (src.dataHex || '').toUpperCase(),
    frameCount: src.frameCount ?? 0,
    totalEdges: src.totalEdges ?? 0,
  })
  // IR 编辑模式：列表数据不含v2解码详情，单独拉取完整信号供展示
  irDetail.value = null
  if (form.kind === 'IR' && form.index != null) {
    getSignal(form.index).then(data => { irDetail.value = data }).catch(() => {})
  }
})

// IR v2 解码详情（旧数据 frameBits 全为 0 时视为无详情）
const irDetail = ref(null)
const hasIrFrames = computed(() =>
  irDetail.value && Array.isArray(irDetail.value.frameBits) && irDetail.value.frameBits.some(b => b > 0))

function onBitLengthChange () {
  if (form.dataHex && form.dataHex.length > maxHexDigits.value) {
    form.dataHex = form.dataHex.substring(0, maxHexDigits.value)
  }
}

function onDataHexInput (event) {
  let value = event.target.value || ''
  value = value.replace(/[^0-9A-Fa-f]/g, '').toUpperCase()
  if (value.length > maxHexDigits.value) value = value.substring(0, maxHexDigits.value)
  form.dataHex = value
}

async function save () {
  if (!formValid.value) return
  saving.value = true
  try {
    if (isIr.value) {
      // 红外：仅更新名称
      await updateSignal({ index: form.index, name: form.name })
    } else if (isEdit.value) {
      await updateSignal({
        index: form.index,
        name: form.name,
        frequency: form.frequency,
        bitCount: form.bitCount,
        shortPulse: form.shortPulse,
        longPulse: form.longPulse,
        syncPulse: form.syncPulse,
        protocolName: form.protocolName || '',
        dataHex: form.dataHex,
      })
    } else {
      await addSignal({
        name: form.name,
        frequency: form.frequency,
        bitCount: form.bitCount,
        shortPulse: form.shortPulse,
        longPulse: form.longPulse,
        syncPulse: form.syncPulse,
        protocolName: form.protocolName || '',
        dataHex: form.dataHex,
      })
    }
    app.notify(t('signals.saved'))
    emit('saved')
    emit('update:modelValue', false)
  } catch (e) {
    // http 拦截器已弹出错误提示
  } finally {
    saving.value = false
  }
}
</script>
