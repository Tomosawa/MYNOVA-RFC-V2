<template>
  <v-dialog
    :model-value="modelValue"
    persistent
    width="auto"
    @update:model-value="$emit('update:modelValue', $event)"
  >
    <v-card>
      <v-card-title class="text-h6">{{ title }}</v-card-title>
      <v-card-text class="text-body-1">{{ message }}</v-card-text>
      <v-card-actions>
        <v-spacer></v-spacer>
        <v-btn color="green-darken-1" variant="text" @click="$emit('update:modelValue', false)">
          {{ t('common.cancel') }}
        </v-btn>
        <v-btn :color="confirmColor" variant="flat" :loading="loading" @click="$emit('confirm')">
          {{ confirmText || t('common.confirm') }}
        </v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>

<script setup>
import { useI18n } from 'vue-i18n'

defineProps({
  modelValue: { type: Boolean, default: false },
  title: { type: String, required: true },
  message: { type: String, default: '' },
  confirmText: { type: String, default: '' },
  confirmColor: { type: String, default: 'red-lighten-1' },
  loading: { type: Boolean, default: false },
})

defineEmits(['update:modelValue', 'confirm'])

const { t } = useI18n()
</script>
