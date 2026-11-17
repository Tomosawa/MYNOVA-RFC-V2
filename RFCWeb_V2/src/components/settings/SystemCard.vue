<template>
  <v-card elevation="2" class="mb-4">
    <v-card-title class="text-subtitle-1">
      <v-icon class="mr-2">tune</v-icon>{{ t('settings.system') }}
    </v-card-title>

    <v-card-text v-if="loaded">
      <v-row dense>
        <v-col cols="12">
          <div class="text-body-2 mb-1">{{ t('settings.brightness') }}: {{ form.brightness }}%</div>
          <v-slider
            v-model="form.brightness"
            :min="0" :max="100" :step="5"
            color="teal"
            hide-details
          ></v-slider>
        </v-col>

        <v-col cols="12" sm="6">
          <v-switch
            v-model="form.soundAlertEnable"
            :label="t('settings.sound')"
            color="teal"
            density="compact"
            hide-details
          ></v-switch>
        </v-col>

        <v-col cols="12" sm="6">
          <v-select
            v-model="form.bootAnim"
            :items="bootAnimItems"
            :label="t('settings.bootAnim')"
            variant="outlined"
            density="compact"
            hide-details
          ></v-select>
        </v-col>

        <v-col cols="6" sm="3">
          <v-select
            v-model="form.autoSleepTime"
            :items="sleepItems"
            :label="t('settings.autoSleep')"
            variant="outlined"
            density="compact"
            hide-details
          ></v-select>
        </v-col>

        <v-col cols="6" sm="3">
          <v-select
            v-model="form.autoScreenOffTime"
            :items="screenOffItems"
            :label="t('settings.autoScreenOff')"
            variant="outlined"
            density="compact"
            hide-details
          ></v-select>
        </v-col>

        <v-col cols="6" sm="3">
          <v-number-input
            v-model="form.repeatTransmit"
            :label="t('settings.repeatTransmit')"
            :min="1" :max="50"
            variant="outlined"
            density="compact"
            control-variant="stacked"
            hide-details
          ></v-number-input>
        </v-col>

        <v-col cols="6" sm="3">
          <v-select
            v-model="form.txPowerLevel"
            :items="txPowerItems"
            :label="t('settings.txPower')"
            variant="outlined"
            density="compact"
            hide-details
          ></v-select>
        </v-col>

        <v-col cols="12" sm="6">
          <v-select
            v-model="form.rxSensitivity"
            :items="rxSensItems"
            :label="t('settings.rxSensitivity')"
            variant="outlined"
            density="compact"
            hide-details
          ></v-select>
        </v-col>
      </v-row>
    </v-card-text>

    <v-card-text v-else class="text-center py-6">
      <v-progress-circular v-if="loadingCard" indeterminate color="teal"></v-progress-circular>
      <template v-else>
        <div class="text-body-2 text-medium-emphasis mb-2">{{ t('settings.loadFail') }}</div>
        <v-btn color="teal" variant="tonal" size="small" prepend-icon="refresh" @click="load">{{ t('common.refresh') }}</v-btn>
      </template>
    </v-card-text>

    <v-divider></v-divider>
    <v-card-actions>
      <v-spacer></v-spacer>
      <v-btn
        color="teal"
        variant="tonal"
        :loading="saving"
        :disabled="!loaded || !dirty"
        @click="save"
      >{{ t('common.save') }}</v-btn>
    </v-card-actions>
  </v-card>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { getSettings, saveSettings } from '@/api/device'

const { t } = useI18n()
const app = useAppStore()

const loaded = ref(false)
const loadingCard = ref(false)
const saving = ref(false)
const original = ref(null)

const form = reactive({
  brightness: 100,
  soundAlertEnable: true,
  repeatTransmit: 15,
  txPowerLevel: 7,
  rxSensitivity: 1,
  autoSleepTime: 0,
  autoScreenOffTime: 0,
  bootAnim: 1,
})

onMounted(load)

async function load () {
  loadingCard.value = true
  try {
    const data = await getSettings({ _silent: true })
    Object.assign(form, data)
    original.value = { ...form }
    loaded.value = true
  } catch (e) {
    loaded.value = false   // 显示加载失败+重试（设备离线或旧固件无端点）
  } finally {
    loadingCard.value = false
  }
}

// 仅提交发生变化的字段
const dirty = computed(() => {
  if (!original.value) return false
  return Object.keys(form).some(k => form[k] !== original.value[k])
})

const bootAnimItems = computed(() => [0, 1, 2, 3, 4, 5].map(v => ({
  title: t(`settings.boot${v}`), value: v,
})))

// 设备端档位：休眠 关/1/3/5/10/30/60 分钟；息屏 关/10s/30s/1/3/5 分钟
const sleepItems = computed(() => [
  { title: t('settings.off'), value: 0 },
  { title: `1 ${t('dashboard.minutes')}`, value: 60000 },
  { title: `3 ${t('dashboard.minutes')}`, value: 180000 },
  { title: `5 ${t('dashboard.minutes')}`, value: 300000 },
  { title: `10 ${t('dashboard.minutes')}`, value: 600000 },
  { title: `30 ${t('dashboard.minutes')}`, value: 1800000 },
  { title: `60 ${t('dashboard.minutes')}`, value: 3600000 },
])
const screenOffItems = computed(() => [
  { title: t('settings.off'), value: 0 },
  { title: `10 ${t('common.seconds')}`, value: 10000 },
  { title: `30 ${t('common.seconds')}`, value: 30000 },
  { title: `1 ${t('dashboard.minutes')}`, value: 60000 },
  { title: `3 ${t('dashboard.minutes')}`, value: 180000 },
  { title: `5 ${t('dashboard.minutes')}`, value: 300000 },
])

// CC1101 发射功率 8 档（与固件 PATABLE 一致）
const txPowerItems = computed(() =>
  ['-30', '-20', '-15', '-10', '-5', '0', '+5', '+10'].map((dbm, i) => ({
    title: `${i} · ${dbm} dBm`, value: i,
  })))

const rxSensItems = computed(() => [0, 1, 2].map(v => ({
  title: t(`settings.rx${v}`), value: v,
})))

async function save () {
  saving.value = true
  try {
    const changed = {}
    Object.keys(form).forEach(k => {
      if (form[k] !== original.value[k]) changed[k] = form[k]
    })
    await saveSettings(changed)
    original.value = { ...form }
    app.notify(t('settings.systemSaved'))
  } catch (e) {
    /* 拦截器已提示（含范围校验失败） */
  } finally {
    saving.value = false
  }
}
</script>
