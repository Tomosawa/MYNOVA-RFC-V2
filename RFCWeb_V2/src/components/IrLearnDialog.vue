<template>
  <v-dialog
    :model-value="modelValue"
    max-width="520"
    persistent
    @update:model-value="onClose"
  >
    <v-card>
      <v-card-title class="text-h6">
        <v-icon class="mr-2">settings_remote</v-icon>{{ t('learn.title') }}
      </v-card-title>

      <v-card-text>
        <!-- 待机 -->
        <template v-if="phase === 'idle'">
          <div class="text-center py-4">
            <v-icon size="56" color="orange">settings_remote</v-icon>
            <p class="text-caption text-medium-emphasis mt-2">{{ t('learn.learning') }}</p>
          </div>
          <v-btn block color="orange" variant="tonal" size="large" prepend-icon="play_arrow"
                 :loading="starting" @click="start">
            {{ t('learn.start') }}
          </v-btn>
        </template>

        <!-- 学习中 -->
        <template v-else-if="phase === 'learning'">
          <div class="text-center py-4">
            <v-progress-circular indeterminate color="orange" size="48" width="4" class="mb-3"></v-progress-circular>
            <div class="text-body-1">{{ t('learn.learning') }}</div>
            <div class="text-h6 mt-2">{{ t('learn.frames', { n: frames }) }}</div>
          </div>
          <v-btn block variant="outlined" color="error" @click="cancel">
            {{ t('learn.cancel') }}
          </v-btn>
        </template>

        <!-- 学习完成 -->
        <template v-else>
          <v-alert type="success" variant="tonal" density="compact" class="mb-3">
            {{ t('learn.finished') }}
          </v-alert>
          <v-list density="compact" class="mb-2">
            <v-list-item>
              <v-list-item-title>{{ t('learn.protocol') }}</v-list-item-title>
              <v-list-item-subtitle>{{ result.protocolName || 'RAW' }} | {{ result.frameCount }} {{ t('signals.frames') }}</v-list-item-subtitle>
            </v-list-item>
            <v-list-item v-for="(bits, i) in result.frameBits" :key="i">
              <v-list-item-title>{{ t('learn.bitCount') }} #{{ i + 1 }}</v-list-item-title>
              <v-list-item-subtitle class="mono-hex">
                {{ bits }} bit | 0x{{ (result.frameValues && result.frameValues[i]) || '?' }}
              </v-list-item-subtitle>
            </v-list-item>
            <v-list-item v-if="result.acState">
              <v-list-item-title>{{ t('learn.acState') }}</v-list-item-title>
              <v-list-item-subtitle>{{ result.acState }}</v-list-item-subtitle>
            </v-list-item>
          </v-list>

          <v-text-field
            v-model="name"
            :label="t('learn.saveAs')"
            variant="outlined"
            density="compact"
            :placeholder="t('signals.namePh')"
          ></v-text-field>

          <v-btn block color="orange" variant="tonal" :loading="saving"
                 :disabled="!validName" @click="save">
            {{ t('learn.save') }}
          </v-btn>
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
import { startIrLearn, irLearnStatus, cancelIrLearn, saveIrLearn } from '@/api/ir'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
})

const emit = defineEmits(['update:modelValue', 'saved'])

const { t } = useI18n()
const app = useAppStore()

const phase = ref('idle')      // idle | learning | finished
const starting = ref(false)
const saving = ref(false)
const frames = ref(0)
const result = ref(null)
const name = ref('')

let pollTimer = null

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
    await startIrLearn()
    phase.value = 'learning'
    frames.value = 0
    result.value = null
    // 1s 轮询学习状态
    pollTimer = setInterval(async () => {
      try {
        const st = await irLearnStatus({ _silent: true })
        frames.value = st.frameCount || 0
        if (st.finished) {
          clearPoll()
          result.value = st
          phase.value = 'finished'
        }
      } catch (e) {
        /* 单次轮询失败忽略 */
      }
    }, 1000)
  } catch (e) {
    // busy —— 拦截器已提示
  } finally {
    starting.value = false
  }
}

async function cancel () {
  clearPoll()
  phase.value = 'idle'
  frames.value = 0
  try { await cancelIrLearn({ _silent: true }) } catch (e) { /* 忽略 */ }
}

async function save () {
  if (!validName.value) return
  saving.value = true
  try {
    const data = await saveIrLearn(name.value)
    app.notify(t('learn.saved', { index: data.index }))
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
    if (phase.value === 'learning') cancel()
    clearPoll()
    phase.value = 'idle'
    result.value = null
  }
  emit('update:modelValue', open)
}

onUnmounted(clearPoll)
</script>

<style scoped>
.mono-hex {
  font-family: monospace;
}
</style>
