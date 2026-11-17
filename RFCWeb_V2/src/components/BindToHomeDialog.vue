<template>
  <v-dialog
    :model-value="modelValue"
    max-width="460"
    persistent
    @update:model-value="$emit('update:modelValue', $event)"
  >
    <v-card>
      <v-card-title class="text-h6">
        <v-icon class="mr-2">apps</v-icon>{{ t('home.bindTitle') }}
      </v-card-title>

      <v-card-text>
        <v-select
          v-model="homeId"
          :items="homeItems"
          :label="t('home.active')"
          variant="outlined"
          density="compact"
          class="mb-3"
        ></v-select>

        <v-select
          v-model="keyIndex"
          :items="keyItems"
          :label="t('home.layout')"
          variant="outlined"
          density="compact"
          class="mb-3"
        ></v-select>

        <v-select
          v-model="slot"
          :items="signalItems"
          :label="t('home.bindTitle')"
          variant="outlined"
          density="compact"
        ></v-select>
      </v-card-text>

      <v-divider></v-divider>
      <v-card-actions>
        <v-spacer></v-spacer>
        <v-btn variant="plain" @click="$emit('update:modelValue', false)">{{ t('common.cancel') }}</v-btn>
        <v-btn color="primary" variant="tonal" :loading="saving" :disabled="!canSave" @click="save">
          {{ t('common.save') }}
        </v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { listHomes, saveHome } from '@/api/home'
import { listSignals } from '@/api/signals'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
  signalSlot: { type: Number, default: null },   // 预选信号槽位（来自卡片菜单；null=由用户选择）
  presetHomeId: { type: Number, default: null },  // 预选主页（来自空键点击）
  presetKeyIndex: { type: Number, default: null }, // 预选键位
})

const emit = defineEmits(['update:modelValue', 'saved'])

const { t } = useI18n()
const app = useAppStore()

const homes = ref([])
const activeId = ref(null)
const signals = ref([])
const homeId = ref(null)
const keyIndex = ref(null)
const slot = ref(0)
const saving = ref(false)

watch(() => props.modelValue, async (open) => {
  if (!open) return
  try {
    const [homeData, sigData] = await Promise.all([
      listHomes({ _silent: true }),
      listSignals({ _silent: true }),
    ])
    homes.value = homeData.profiles || []
    activeId.value = homeData.active
    signals.value = sigData.data || []
  } catch (e) {
    /* 拦截器已提示 */
  }
  homeId.value = props.presetHomeId != null ? props.presetHomeId : activeId.value
  keyIndex.value = props.presetKeyIndex != null ? props.presetKeyIndex : 1
  slot.value = props.signalSlot != null ? props.signalSlot : 0
})

const homeItems = computed(() =>
  homes.value.map(h => ({
    title: h.id === activeId.value ? `${h.name} · ${t('home.active')}` : h.name,
    value: h.id,
  })))

const currentProfile = computed(() => homes.value.find(h => h.id === homeId.value) || null)
const keyCount = computed(() => (currentProfile.value ? currentProfile.value.layout + 1 : 0))
const keyItems = computed(() => {
  const items = []
  for (let k = 1; k <= keyCount.value; k++) items.push({ title: t('home.keyN', { n: k }), value: k })
  return items
})

// 信号选择项：0=清除绑定；其余为 RF/IR 槽位（显示类型+名称）
const signalItems = computed(() => {
  const items = [{ title: `— ${t('home.unbind')} —`, value: 0 }]
  signals.value.forEach(s => {
    items.push({
      title: `${s.kind} #${s.index} ${s.name || t('common.unnamed')}`,
      value: s.index,
    })
  })
  return items
})

const canSave = computed(() =>
  homeId.value != null && keyIndex.value != null && keyIndex.value >= 1 && keyIndex.value <= keyCount.value)

async function save () {
  const profile = currentProfile.value
  if (!profile) return
  const slots = [...profile.slots]
  slots[keyIndex.value - 1] = slot.value || 0
  saving.value = true
  try {
    await saveHome({ id: profile.id, name: profile.name, layout: profile.layout, slots })
    app.notify(t('home.saved'))
    emit('saved')
    emit('update:modelValue', false)
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    saving.value = false
  }
}
</script>
