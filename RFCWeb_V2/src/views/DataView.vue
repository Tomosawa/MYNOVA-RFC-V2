<template>
  <v-container fluid class="pb-16">
    <v-tabs v-model="tab" color="teal" density="comfortable">
      <v-tab value="rf">
        <v-icon class="mr-1">radio_button_checked</v-icon>{{ t('dashboard.rfSignals') }}
      </v-tab>
      <v-tab value="ir">
        <v-icon class="mr-1">settings_remote</v-icon>{{ t('dashboard.irSignals') }}
      </v-tab>
      <v-tab value="backup">
        <v-icon class="mr-1">backup</v-icon>{{ t('backup.title') }}
      </v-tab>
    </v-tabs>

    <v-window v-model="tab" class="mt-3">
      <v-window-item value="rf">
        <RfTab
          :signals="signals"
          @add="openEdit(null)"
          @edit="openEdit"
          @detail="openDetail"
          @delete="askDelete"
          @capture="captureDialog = true"
        ></RfTab>
      </v-window-item>

      <v-window-item value="ir">
        <IrTab
          :signals="signals"
          @learn="learnDialog = true"
          @edit="openEdit"
          @detail="openDetail"
          @delete="askDelete"
        ></IrTab>
      </v-window-item>

      <v-window-item value="backup">
        <BackupTab @restored="fetchSignals"></BackupTab>
      </v-window-item>
    </v-window>

    <!-- 添加/编辑 -->
    <SignalEditDialog
      v-model="editDialog"
      :item="editItem"
      @saved="fetchSignals"
    ></SignalEditDialog>

    <!-- 详情 -->
    <SignalDetailDialog
      v-model="detailDialog"
      :index="detailIndex"
    ></SignalDetailDialog>

    <!-- 删除确认 -->
    <ConfirmDialog
      v-model="deleteDialog"
      :title="t('signals.deleteTitle')"
      :message="t('signals.deleteMsg', { name: deleteItem && deleteItem.name })"
      :confirm-text="t('common.delete')"
      :loading="deleting"
      @confirm="doDelete"
    ></ConfirmDialog>

    <!-- RF 捕获 -->
    <RfCaptureDialog v-model="captureDialog" @saved="fetchSignals"></RfCaptureDialog>

    <!-- IR 学习 -->
    <IrLearnDialog v-model="learnDialog" @saved="fetchSignals"></IrLearnDialog>
  </v-container>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { listSignals, deleteSignal } from '@/api/signals'
import RfTab from '@/components/data/RfTab.vue'
import IrTab from '@/components/data/IrTab.vue'
import BackupTab from '@/components/data/BackupTab.vue'
import SignalEditDialog from '@/components/SignalEditDialog.vue'
import SignalDetailDialog from '@/components/SignalDetailDialog.vue'
import ConfirmDialog from '@/components/ConfirmDialog.vue'
import RfCaptureDialog from '@/components/RfCaptureDialog.vue'
import IrLearnDialog from '@/components/IrLearnDialog.vue'

const route = useRoute()
const router = useRouter()
const { t } = useI18n()
const app = useAppStore()

// 子 tab 与 URL query 双向同步（#/data?tab=ir 刷新不丢失）
const tab = computed({
  get: () => route.query.tab || 'rf',
  set: (v) => router.replace({ query: { ...route.query, tab: v } }),
})

const signals = ref([])
const loading = ref(false)

async function fetchSignals () {
  loading.value = true
  try {
    const data = await listSignals({ _silent: true })
    signals.value = data.data || []
  } catch (e) {
    app.notify(t('signals.loadFail'), 'error')
  } finally {
    loading.value = false
  }
}
onMounted(fetchSignals)

// ── 编辑对话框 ──
const editDialog = ref(false)
const editItem = ref(null)
function openEdit (item) {
  editItem.value = item   // null = 新增
  editDialog.value = true
}

// ── 详情对话框 ──
const detailDialog = ref(false)
const detailIndex = ref(null)
function openDetail (item) {
  detailIndex.value = item.index
  detailDialog.value = true
}

// ── 删除确认 ──
const deleteDialog = ref(false)
const deleteItem = ref(null)
const deleting = ref(false)
function askDelete (item) {
  deleteItem.value = item
  deleteDialog.value = true
}
async function doDelete () {
  deleting.value = true
  try {
    await deleteSignal(deleteItem.value.index)
    app.notify(t('signals.deleted'))
    deleteDialog.value = false
    await fetchSignals()
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    deleting.value = false
  }
}

// ── 捕获/学习对话框 ──
const captureDialog = ref(false)
const learnDialog = ref(false)
</script>
