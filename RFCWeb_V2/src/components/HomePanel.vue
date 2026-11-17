<template>
  <div>
    <!-- 主页选择 + 管理入口 -->
    <v-row dense class="mb-3">
      <v-col cols="9">
        <v-select
          v-model="selected"
          :items="homeItems"
          :label="t('home.panelTitle')"
          variant="outlined"
          density="compact"
          hide-details
        ></v-select>
      </v-col>
      <v-col cols="3" class="text-right">
        <v-btn block color="teal" variant="tonal" size="small" height="40" prepend-icon="edit" @click="$emit('manage')">
          {{ t('home.manage') }}
        </v-btn>
      </v-col>
    </v-row>

    <!-- 主页键位网格（模拟设备屏幕布局） -->
    <v-card v-if="profile" elevation="2" class="pa-3">
      <div class="text-caption text-medium-emphasis mb-2">{{ t('home.tapToSend') }}</div>
      <div class="home-grid" :style="gridStyle">
        <v-card
          v-for="key in keyCount"
          :key="key"
          class="key-cell"
          :class="{ 'key-cell-full': isFirstFull && key === 1 }"
          :color="cellSignal(key) ? (cellSignal(key).kind === 'IR' ? 'orange' : 'primary') : 'default'"
          :variant="cellSignal(key) ? 'tonal' : 'outlined'"
          :loading="sendingKey === key"
          @click="onKeyClick(key)"
        >
          <template v-if="cellSignal(key)">
            <v-icon size="20">{{ cellSignal(key).kind === 'IR' ? 'settings_remote' : 'radio_button_checked' }}</v-icon>
            <div class="key-name">{{ cellSignal(key).name || t('common.unnamed') }}</div>
          </template>
          <template v-else>
            <v-icon size="20">add</v-icon>
            <div class="key-name text-medium-emphasis">{{ t('home.keyN', { n: key }) }}</div>
          </template>
        </v-card>
      </div>
    </v-card>

    <v-card v-else variant="outlined" class="text-center pa-8">
      <v-icon size="56" color="grey">apps</v-icon>
      <p class="text-caption text-medium-emphasis mt-2">{{ t('home.noProfiles') }}</p>
    </v-card>
  </div>
</template>

<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { listHomes } from '@/api/home'
import { sendSignal as apiSendSignal } from '@/api/signals'

const props = defineProps({
  signals: { type: Array, default: () => [] },   // 全部信号（供键位显示名称）
  version: { type: Number, default: 0 },         // 递增强制重载
})

const emit = defineEmits(['bind-key', 'manage'])

const { t } = useI18n()
const app = useAppStore()

const homes = ref([])
const activeId = ref(null)
const selected = ref('active')   // 'active' | 主页id
const sendingKey = ref(null)

async function load () {
  try {
    const data = await listHomes({ _silent: true })
    homes.value = data.profiles || []
    activeId.value = data.active
  } catch (e) {
    homes.value = []
  }
}
onMounted(load)
watch(() => props.version, load)

const homeItems = computed(() => {
  const items = []
  const active = homes.value.find(h => h.id === activeId.value)
  if (active) items.push({ title: `${active.name} · ${t('home.active')}`, value: 'active' })
  homes.value.forEach(h => {
    if (h.id !== activeId.value) items.push({ title: h.name, value: h.id })
  })
  return items
})

const profile = computed(() => {
  if (selected.value === 'active') return homes.value.find(h => h.id === activeId.value) || homes.value[0] || null
  return homes.value.find(h => h.id === selected.value) || null
})

// 布局 = 键数（1..9）；1/3/5/7 的首键占满整行（与设备屏幕一致）
const keyCount = computed(() => (profile.value ? profile.value.layout + 1 : 0))
const isFirstFull = computed(() => [1, 3, 5, 7].includes(keyCount.value))
const gridStyle = computed(() => {
  const cols = (keyCount.value === 1 || keyCount.value === 7 || keyCount.value === 9) ? 3 : 2
  return {
    display: 'grid',
    gridTemplateColumns: `repeat(${cols}, 1fr)`,
    gap: '8px',
  }
})

function cellSignal (key) {
  const slot = profile.value ? profile.value.slots[key - 1] : 0
  if (!slot) return null
  return props.signals.find(s => s.index === slot) || null
}

async function onKeyClick (key) {
  const slot = profile.value ? profile.value.slots[key - 1] : 0
  if (!slot) {
    // 空键 → 绑定
    emit('bind-key', { homeId: profile.value.id, keyIndex: key })
    return
  }
  if (sendingKey.value !== null) return
  sendingKey.value = key
  try {
    await apiSendSignal(slot)
    const sig = cellSignal(key)
    app.notify(t('remote.sendOk', { name: (sig && sig.name) || t('common.unnamed') }))
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    sendingKey.value = null
  }
}
</script>

<style scoped>
.home-grid .key-cell {
  height: 84px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  text-align: center;
  transition: transform 0.15s ease;
}
.home-grid .key-cell:active {
  transform: scale(0.96);
}
.key-cell-full {
  grid-column: 1 / -1;
  height: 96px !important;
}
.key-name {
  font-size: 12px;
  margin-top: 4px;
  padding: 0 6px;
  max-width: 100%;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
