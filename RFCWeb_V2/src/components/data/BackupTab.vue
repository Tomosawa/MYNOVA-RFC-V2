<template>
  <div>
    <!-- 导出 -->
    <v-btn
      block color="primary" variant="tonal" class="mb-4"
      prepend-icon="upload_file"
      :loading="exporting"
      @click="doExport"
    >{{ t('backup.export') }}</v-btn>

    <!-- 备份文件列表 -->
    <template v-if="files.length > 0">
      <div class="text-subtitle-2 text-medium-emphasis mb-2">{{ t('backup.files') }}</div>
      <v-list border density="compact" class="py-0">
        <v-list-item v-for="file in files" :key="file" density="compact">
          <v-list-item-title class="text-body-2">{{ file }}</v-list-item-title>
          <template #append>
            <v-btn icon="restore" size="small" variant="text" color="primary"
                   @click="openRestore(file)"></v-btn>
            <v-btn icon="delete" size="small" variant="text" color="error"
                   @click="askDelete(file)"></v-btn>
          </template>
        </v-list-item>
      </v-list>
    </template>
    <v-card v-else variant="outlined" class="text-center pa-4">
      <p class="text-caption text-medium-emphasis">{{ t('backup.noFiles') }}</p>
    </v-card>

    <!-- 恢复选项对话框 -->
    <v-dialog v-model="restoreDialog" max-width="420" persistent>
      <v-card>
        <v-card-title class="text-h6">{{ t('backup.restoreTitle') }}</v-card-title>
        <v-card-subtitle class="text-caption text-wrap">{{ restoreFile }}</v-card-subtitle>
        <v-card-text>
          <v-alert type="warning" variant="tonal" density="compact" class="mb-3">
            {{ t('backup.restoreWarn') }}
          </v-alert>
          <v-checkbox v-model="restoreOptions.signals" :label="t('backup.restoreSignals')" density="compact" hide-details></v-checkbox>
          <v-checkbox v-model="restoreOptions.system" :label="t('backup.restoreSystem')" density="compact" hide-details></v-checkbox>
        </v-card-text>
        <v-card-actions>
          <v-spacer></v-spacer>
          <v-btn color="green-darken-1" variant="text" @click="restoreDialog = false">{{ t('common.cancel') }}</v-btn>
          <v-btn color="primary" variant="flat" :loading="restoring"
                 :disabled="!restoreOptions.signals && !restoreOptions.system"
                 @click="doRestore">{{ t('common.confirm') }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- 删除备份确认 -->
    <ConfirmDialog
      v-model="deleteDialog"
      :title="t('backup.deleteTitle')"
      :message="t('backup.deleteMsg', { file: deleteFile })"
      :confirm-text="t('common.delete')"
      :loading="deleting"
      @confirm="doDelete"
    ></ConfirmDialog>
  </div>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { listBackups, exportBackup, restoreBackup, deleteBackup } from '@/api/backup'
import ConfirmDialog from '@/components/ConfirmDialog.vue'

const emit = defineEmits(['restored'])

const { t } = useI18n()
const app = useAppStore()

const files = ref([])
const exporting = ref(false)
const restoring = ref(false)
const deleting = ref(false)

const restoreDialog = ref(false)
const restoreFile = ref('')
const restoreOptions = ref({ signals: true, system: true })
const deleteDialog = ref(false)
const deleteFile = ref('')

onMounted(() => fetchBackups())

async function fetchBackups () {
  try {
    const data = await listBackups({ _silent: true })
    files.value = data.files || []
  } catch (e) {
    app.notify(t('backup.listFail'), 'error')
  }
}

async function doExport () {
  exporting.value = true
  try {
    const data = await exportBackup()
    app.notify(t('backup.exported', { file: data.file || '' }))
    await fetchBackups()
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    exporting.value = false
  }
}

function openRestore (file) {
  restoreFile.value = file
  restoreOptions.value = { signals: true, system: true }
  restoreDialog.value = true
}

async function doRestore () {
  restoring.value = true
  try {
    const data = await restoreBackup(restoreFile.value, restoreOptions.value.signals, restoreOptions.value.system)
    if (data.result === 'OK') {
      restoreDialog.value = false
      app.notify(data.message || t('backup.restoreOk'))
      emit('restored')   // 信号可能已被恢复，通知父级刷新列表
    }
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    restoring.value = false
  }
}

function askDelete (file) {
  deleteFile.value = file
  deleteDialog.value = true
}

async function doDelete () {
  deleting.value = true
  try {
    await deleteBackup(deleteFile.value)
    app.notify(t('backup.deleted'))
    deleteDialog.value = false
    await fetchBackups()
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    deleting.value = false
  }
}
</script>
