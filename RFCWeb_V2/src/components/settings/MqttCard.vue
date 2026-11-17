<template>
  <v-card elevation="2" class="mb-4">
    <v-card-title class="text-subtitle-1">
      <v-icon class="mr-2">home_work</v-icon>{{ t('settings.mqtt') }}
    </v-card-title>

    <v-card-text>
      <v-form v-model="haValid" @submit.prevent="onSubmit">
        <v-radio-group v-model="haMode" inline density="compact">
          <v-radio :label="t('settings.mqttAuto')" value="auto" color="success"></v-radio>
          <v-radio :label="t('settings.mqttManual')" value="manual" color="success"></v-radio>
        </v-radio-group>

        <v-text-field
          v-if="haMode === 'manual'"
          v-model="haServer"
          :label="t('settings.mqttServer')"
          variant="outlined"
          density="compact"
          :rules="haMode === 'manual' ? [v => !!v || ''] : []"
          prepend-inner-icon="dns"
          class="mb-3"
        ></v-text-field>

        <v-text-field
          v-if="haMode === 'manual'"
          v-model.number="haPort"
          type="number"
          :label="t('settings.mqttPort')"
          variant="outlined"
          density="compact"
          prepend-inner-icon="settings_ethernet"
          class="mb-3"
        ></v-text-field>

        <v-text-field
          v-model="haUsername"
          :label="t('settings.mqttUser')"
          variant="outlined"
          density="compact"
          prepend-inner-icon="person"
          class="mb-3"
        ></v-text-field>

        <v-text-field
          v-model="haPassword"
          :type="showPwd ? 'text' : 'password'"
          :label="t('settings.mqttPwd')"
          variant="outlined"
          density="compact"
          prepend-inner-icon="lock"
          :append-inner-icon="showPwd ? 'visibility_off' : 'visibility'"
          @click:append-inner="showPwd = !showPwd"
          class="mb-3"
        ></v-text-field>

        <v-btn block color="success" variant="elevated" size="large" type="submit"
               :disabled="!haValid" :loading="loading">
          {{ t('settings.mqttSave') }}
        </v-btn>
        <v-btn block color="warning" variant="text" class="mt-2" size="large"
               :disabled="loading" @click="testConnection">
          {{ t('settings.mqttTest') }}
        </v-btn>
        <v-btn block color="error" variant="text" class="mt-1" size="large"
               :disabled="loading" @click="clearDialog = true">
          {{ t('settings.mqttClear') }}
        </v-btn>
      </v-form>

      <v-alert v-if="status.show" :type="status.type" variant="tonal" density="compact" class="mt-4">
        {{ status.message }}
      </v-alert>
    </v-card-text>

    <!-- 清除配置确认（统一 Vuetify 弹窗，替代原生 confirm） -->
    <ConfirmDialog
      v-model="clearDialog"
      :title="t('common.confirm')"
      :message="t('settings.mqttClearConfirm')"
      :confirm-text="t('common.confirm')"
      @confirm="doClear"
    ></ConfirmDialog>
  </v-card>
</template>

<script setup>
import { onMounted, reactive, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { getMqttConfig, saveMqttConfig, clearMqttConfig, connectMqtt, mqttStatus } from '@/api/mqtt'
import ConfirmDialog from '@/components/ConfirmDialog.vue'

const { t } = useI18n()

const haValid = ref(false)
const loading = ref(false)
const showPwd = ref(false)
const haMode = ref('auto')
const haServer = ref('')
const haPort = ref(1883)
const haUsername = ref('')
const haPassword = ref('')

const clearDialog = ref(false)
const status = reactive({ show: false, type: 'info', message: '' })

onMounted(async () => {
  try {
    const cfg = await getMqttConfig({ _silent: true })
    if (cfg.hasConfig) {
      if (cfg.server && cfg.server.length > 0) {
        haMode.value = 'manual'
        haServer.value = cfg.server
        haPort.value = cfg.port || 1883
      } else {
        haMode.value = 'auto'
      }
      haUsername.value = cfg.username || ''
      haPassword.value = cfg.password || ''
    }
  } catch (e) { /* 忽略 */ }
})

async function onSubmit () {
  if (!haValid.value) return
  loading.value = true
  status.show = false
  try {
    await saveMqttConfig({
      mode: haMode.value,
      server: haMode.value === 'manual' ? haServer.value : '',
      port: haMode.value === 'manual' ? Number(haPort.value) : 1883,
      username: haUsername.value,
      password: haPassword.value,
    })
    status.show = true
    status.type = 'success'
    status.message = t('settings.mqttConnecting')
    await doConnect()
  } catch (e) {
    status.show = true
    status.type = 'error'
    status.message = e.message
  } finally {
    loading.value = false
  }
}

async function doConnect () {
  try {
    const data = await connectMqtt({ _silent: true })
    if (data.connected) {
      status.type = 'success'
      status.message = t('settings.mqttConnected')
    } else {
      status.type = 'warning'
      status.message = data.message || t('settings.wifiConnectFail')
    }
  } catch (e) {
    status.type = 'error'
    status.message = e.message
  }
  status.show = true
}

async function testConnection () {
  loading.value = true
  status.show = false
  try {
    const data = await mqttStatus({ _silent: true })
    if (data.connected) {
      status.type = 'success'
      status.message = t('settings.mqttConnected')
    } else if (data.hasConfig) {
      status.type = 'warning'
      status.message = t('settings.mqttDisconnected')
      await doConnect()
    } else {
      status.type = 'info'
      status.message = t('settings.mqttNotConfigured')
    }
  } catch (e) {
    status.type = 'error'
    status.message = e.message
  } finally {
    loading.value = false
    status.show = true
  }
}

async function doClear () {
  clearDialog.value = false
  loading.value = true
  try {
    await clearMqttConfig()
    haMode.value = 'auto'
    haServer.value = ''
    haPort.value = 1883
    haUsername.value = ''
    haPassword.value = ''
    status.show = true
    status.type = 'info'
    status.message = t('common.success')
  } catch (e) {
    status.show = true
    status.type = 'error'
    status.message = e.message
  } finally {
    loading.value = false
  }
}
</script>
