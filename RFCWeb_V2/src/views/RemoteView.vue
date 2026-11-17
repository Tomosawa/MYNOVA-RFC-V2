<template>
  <v-container fluid class="pb-16">
    <!-- 工具栏：视图切换 + 搜索（仅网格模式） -->
    <v-row dense class="mb-3">
      <v-col cols="7" sm="9">
        <v-text-field
          v-if="mode === 'grid'"
          v-model="search"
          :label="t('remote.searchPh')"
          prepend-inner-icon="search"
          variant="outlined"
          density="compact"
          clearable
          hide-details
        ></v-text-field>
      </v-col>
      <v-col :cols="mode === 'grid' ? 5 : 12" :sm="mode === 'grid' ? 3 : 12" class="text-right">
        <v-btn-toggle v-model="mode" mandatory density="compact" color="teal">
          <v-btn value="grid" icon="grid_view" :title="t('home.overview')"></v-btn>
          <v-btn value="home" icon="apps" :title="t('home.panelTitle')"></v-btn>
        </v-btn-toggle>
      </v-col>
    </v-row>

    <!-- 槽位总览网格 -->
    <template v-if="mode === 'grid'">
      <v-row v-if="filtered.length > 0" dense>
        <v-col v-for="item in filtered" :key="item.index" cols="6" sm="4" md="3">
          <v-card
            elevation="2"
            class="control-button"
            :color="sendingIndex === item.index ? (item.kind === 'IR' ? 'orange' : 'primary') : 'default'"
            :loading="sendingIndex === item.index"
            @click="sendSignal(item)"
          >
            <v-card-text class="text-center pa-4 pb-2">
              <v-icon
                size="44"
                :color="sendingIndex === item.index ? 'white' : (item.kind === 'IR' ? 'orange' : 'primary')"
              >{{ item.kind === 'IR' ? 'settings_remote' : 'radio_button_checked' }}</v-icon>
              <div class="text-subtitle-1 mt-2" :class="sendingIndex === item.index ? 'text-white' : ''">
                {{ item.name || t('common.unnamed') }}
              </div>
              <div class="text-caption mt-1" :class="sendingIndex === item.index ? 'text-white' : 'text-medium-emphasis'">
                {{ itemSubtitle(item) }}
              </div>
            </v-card-text>

            <!-- 更多操作菜单 -->
            <v-menu>
              <template #activator="{ props: menuProps }">
                <v-btn
                  v-bind="menuProps"
                  icon="more_vert"
                  size="x-small"
                  variant="text"
                  class="card-menu-btn"
                  @click.stop
                ></v-btn>
              </template>
              <v-list density="compact">
                <v-list-item prepend-icon="play_arrow" :title="t('remote.menuSend')" @click="sendSignal(item)"></v-list-item>
                <v-list-item prepend-icon="edit" :title="t('remote.menuEdit')" @click="editItem = item; editDialog = true"></v-list-item>
                <v-list-item prepend-icon="apps" :title="t('remote.menuBind')" @click="openBind(item.index, null, null)"></v-list-item>
                <v-list-item prepend-icon="visibility" :title="t('common.detail')" @click="detailIndex = item.index; detailDialog = true"></v-list-item>
                <v-list-item prepend-icon="delete" :title="t('remote.menuDelete')" base-color="error" @click="deleteItemSel = item; deleteDialog = true"></v-list-item>
              </v-list>
            </v-menu>
          </v-card>
        </v-col>
      </v-row>

      <v-card v-else variant="outlined" class="text-center pa-8">
        <v-icon size="64" color="grey">inbox</v-icon>
        <p class="text-medium-emphasis mt-2">{{ t('remote.noData') }}</p>
        <p class="text-caption text-medium-emphasis">{{ t('remote.noDataHint') }}</p>
      </v-card>
    </template>

    <!-- 设备主页面板模式 -->
    <HomePanel
      v-else
      :signals="signals"
      :version="homePanelVersion"
      @bind-key="({ homeId, keyIndex }) => openBind(null, homeId, keyIndex)"
      @manage="manageDialog = true"
    ></HomePanel>

    <!-- 编辑 -->
    <SignalEditDialog v-model="editDialog" :item="editItem" @saved="fetchSignals"></SignalEditDialog>

    <!-- 详情 -->
    <SignalDetailDialog v-model="detailDialog" :index="detailIndex"></SignalDetailDialog>

    <!-- 删除确认 -->
    <ConfirmDialog
      v-model="deleteDialog"
      :title="t('signals.deleteTitle')"
      :message="t('signals.deleteMsg', { name: deleteItemSel && deleteItemSel.name })"
      :confirm-text="t('common.delete')"
      :loading="deleting"
      @confirm="doDelete"
    ></ConfirmDialog>

    <!-- 绑定到主页（预选信号 或 预选主页+键位） -->
    <BindToHomeDialog
      v-model="bindDialog"
      :signal-slot="bindSignalSlot"
      :preset-home-id="bindPresetHome"
      :preset-key-index="bindPresetKey"
      @saved="homePanelVersion++"
    ></BindToHomeDialog>

    <!-- 主页管理 -->
    <HomeManageDialog
      v-model="manageDialog"
      :signals="signals"
      @changed="homePanelVersion++"
    ></HomeManageDialog>
  </v-container>
</template>

<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { listSignals, sendSignal as apiSend, deleteSignal } from '@/api/signals'
import SignalEditDialog from '@/components/SignalEditDialog.vue'
import SignalDetailDialog from '@/components/SignalDetailDialog.vue'
import ConfirmDialog from '@/components/ConfirmDialog.vue'
import BindToHomeDialog from '@/components/BindToHomeDialog.vue'
import HomePanel from '@/components/HomePanel.vue'
import HomeManageDialog from '@/components/HomeManageDialog.vue'

const { t } = useI18n()
const app = useAppStore()
const route = useRoute()
const router = useRouter()

// ── 视图模式：槽位总览 / 主页面板（支持 ?view=home 深链，如仪表盘统计卡跳转） ──
const mode = ref(route.query.view === 'home' ? 'home' : 'grid')
watch(mode, v => router.replace({ query: v === 'home' ? { view: 'home' } : {} }))
const search = ref('')
const homePanelVersion = ref(0)   // 递增强制 HomePanel 重载

const signals = ref([])

async function fetchSignals () {
  try {
    const data = await listSignals({ _silent: true })
    signals.value = data.data || []
  } catch (e) {
    app.notify(t('signals.loadFail'), 'error')
  }
}

onMounted(fetchSignals)

const filtered = computed(() => {
  if (!search.value) return signals.value
  const q = search.value.toLowerCase()
  return signals.value.filter(i =>
    (i.name || '').toLowerCase().includes(q) || String(i.index).includes(q))
})

function itemSubtitle (item) {
  if (item.kind === 'IR') {
    return `IR ${item.protocolName || 'RAW'} · ${item.frameCount}×`
  }
  return `${item.frequency}MHz | ${item.protocolName || 'RAW'} | ${item.bitCount}bit`
}

// ── 发送（防连点） ──
const sendingIndex = ref(null)
async function sendSignal (item) {
  if (sendingIndex.value !== null) return
  sendingIndex.value = item.index
  try {
    await apiSend(item.index)
    app.notify(t('remote.sendOk', { name: item.name || t('common.unnamed') }))
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    sendingIndex.value = null
  }
}

// ── 编辑 / 详情 / 删除 ──
const editDialog = ref(false)
const editItem = ref(null)
const detailDialog = ref(false)
const detailIndex = ref(null)
const deleteDialog = ref(false)
const deleteItemSel = ref(null)
const deleting = ref(false)

async function doDelete () {
  deleting.value = true
  try {
    await deleteSignal(deleteItemSel.value.index)
    app.notify(t('signals.deleted'))
    deleteDialog.value = false
    await fetchSignals()
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    deleting.value = false
  }
}

// ── 绑定到主页 ──
const bindDialog = ref(false)
const bindSignalSlot = ref(null)   // 预选信号槽位（来自卡片菜单）
const bindPresetHome = ref(null)   // 预选主页（来自空键点击）
const bindPresetKey = ref(null)    // 预选键位（来自空键点击）

function openBind (signalSlot, presetHomeId, presetKeyIndex) {
  bindSignalSlot.value = signalSlot
  bindPresetHome.value = presetHomeId
  bindPresetKey.value = presetKeyIndex
  bindDialog.value = true
}

// ── 主页管理 ──
const manageDialog = ref(false)
</script>

<style scoped>
.control-button {
  cursor: pointer;
  transition: all 0.25s ease;
  position: relative;
}
.control-button:hover {
  transform: translateY(-4px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}
.control-button:active {
  transform: translateY(-2px);
}
.card-menu-btn {
  position: absolute;
  top: 2px;
  right: 2px;
  opacity: 0.55;
}
</style>
