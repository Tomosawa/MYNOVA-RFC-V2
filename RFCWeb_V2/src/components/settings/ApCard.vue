<template>
  <v-card elevation="2" class="mb-4">
    <v-card-title class="text-subtitle-1">
      <v-icon class="mr-2">wifi_tethering</v-icon>{{ t('settings.ap') }}
    </v-card-title>

    <v-card-text>
      <v-form v-model="apValid" @submit.prevent="askSave">
        <v-text-field
          v-model="apSsid"
          :label="t('settings.apSsid')"
          variant="outlined"
          density="compact"
          :rules="[v => !!v || '']"
          prepend-inner-icon="wifi_tethering"
          class="mb-3"
        ></v-text-field>

        <v-text-field
          v-model="apPassword"
          :type="showPwd ? 'text' : 'password'"
          :label="t('settings.apPwd')"
          variant="outlined"
          density="compact"
          :rules="[v => !!v || '']"
          prepend-inner-icon="lock"
          :append-inner-icon="showPwd ? 'visibility_off' : 'visibility'"
          @click:append-inner="showPwd = !showPwd"
          class="mb-3"
        ></v-text-field>

        <v-btn block color="success" variant="elevated" size="large" type="submit"
               :disabled="!apValid" :loading="loading">
          {{ t('settings.wifiSave') }}
        </v-btn>
      </v-form>

      <v-switch
        v-model="apEnable"
        :label="t('settings.apEnable')"
        color="success"
        inset
        density="compact"
        class="mt-2"
        @update:model-value="onEnableChanged"
      ></v-switch>
    </v-card-text>

    <!-- 风险确认弹窗：输入确认文字才能修改（防误改丢热点密码） -->
    <v-dialog v-model="riskDialog" max-width="420" persistent>
      <v-card>
        <v-card-title class="text-h6">
          <v-icon color="warning" class="mr-2">warning</v-icon>{{ t('settings.apRiskTitle') }}
        </v-card-title>
        <v-card-text>
          <v-alert type="warning" variant="tonal" density="compact" class="mb-3">
            {{ t('settings.apRiskMsg') }}
          </v-alert>
          <v-chip color="red" variant="flat" rounded="0" class="mb-2" block>
            {{ t('settings.apRiskChip', { text: confirmText }) }}
          </v-chip>
          <v-text-field
            v-model="confirmInput"
            variant="outlined"
            density="compact"
            :placeholder="t('settings.apRiskChip', { text: confirmText })"
          ></v-text-field>
        </v-card-text>
        <v-card-actions>
          <v-spacer></v-spacer>
          <v-btn variant="flat" @click="riskDialog = false">{{ t('common.cancel') }}</v-btn>
          <v-btn
            color="red-darken-1"
            variant="flat"
            :disabled="confirmInput !== confirmText"
            :loading="saving"
            @click="doSave"
          >{{ t('settings.apConfirmBtn') }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </v-card>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { saveAp, setApEnable, getApWifiInfo } from '@/api/wifi'

const { t } = useI18n()
const app = useAppStore()

const apValid = ref(false)
const loading = ref(false)
const saving = ref(false)
const showPwd = ref(false)
const apSsid = ref('')
const apPassword = ref('')
const apEnable = ref(false)

const riskDialog = ref(false)
const confirmInput = ref('')

// 确认文字随语言包（中文"我确认风险并修改"，英文"I ACCEPT THE RISK"）
const confirmText = computed(() => t('settings.apConfirmText'))

onMounted(async () => {
  try {
    const info = await getApWifiInfo({ _silent: true })
    apSsid.value = info.AP_SSID || ''
    apPassword.value = info.AP_PWD || ''
    apEnable.value = !!info.AP_ENABLE
  } catch (e) { /* 忽略 */ }
})

function askSave () {
  if (!apValid.value) return
  confirmInput.value = ''
  riskDialog.value = true
}

async function doSave () {
  saving.value = true
  try {
    await saveAp(apSsid.value, apPassword.value)
    app.notify(t('common.success'))
    riskDialog.value = false
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    saving.value = false
  }
}

async function onEnableChanged (v) {
  try {
    await setApEnable(v)
  } catch (e) {
    apEnable.value = !v
  }
}
</script>
