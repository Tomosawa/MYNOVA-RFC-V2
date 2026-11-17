<template>
  <v-container fluid class="pb-16">
    <SystemCard></SystemCard>
    <WifiCard></WifiCard>
    <ApCard></ApCard>
    <MqttCard></MqttCard>
    <MaintenanceCard></MaintenanceCard>
  </v-container>
</template>

<script setup>
import SystemCard from '@/components/settings/SystemCard.vue'
import WifiCard from '@/components/settings/WifiCard.vue'
import ApCard from '@/components/settings/ApCard.vue'
import MqttCard from '@/components/settings/MqttCard.vue'
import MaintenanceCard from '@/components/settings/MaintenanceCard.vue'
</script>
