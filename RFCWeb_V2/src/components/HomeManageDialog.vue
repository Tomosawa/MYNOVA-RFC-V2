<template>
  <v-dialog
    :model-value="modelValue"
    max-width="720"
    persistent
    scrollable
    @update:model-value="$emit('update:modelValue', $event)"
  >
    <v-card>
      <v-card-title class="text-h6 d-flex align-center">
        <v-icon class="mr-2">apps</v-icon>{{ t('home.manage') }}
        <v-spacer></v-spacer>
        <v-btn icon="add" size="small" variant="tonal" color="teal" @click="createOpen = true"></v-btn>
      </v-card-title>

      <v-card-text>
        <!-- 新建主页 -->
        <v-expand-transition>
          <v-card v-if="createOpen" variant="outlined" class="mb-4 pa-3">
            <v-row dense>
              <v-col cols="5">
                <v-text-field
                  v-model="createName"
                  :label="t('home.name')"
                  variant="outlined"
                  density="compact"
                  hide-details
                ></v-text-field>
              </v-col>
              <v-col cols="4">
                <v-select
                  v-model="createLayout"
                  :items="layoutItems"
                  :label="t('home.layout')"
                  variant="outlined"
                  density="compact"
                  hide-details
                ></v-select>
              </v-col>
              <v-col cols="3" class="text-right">
                <v-btn color="teal" variant="tonal" block height="40" :disabled="!createName" :loading="creating" @click="doCreate">
                  {{ t('common.save') }}
                </v-btn>
              </v-col>
            </v-row>
          </v-card>
        </v-expand-transition>

        <!-- 主页列表 -->
        <template v-if="homes.length > 0">
          <v-card
            v-for="h in homes"
            :key="h.id"
            variant="outlined"
            class="mb-3"
          >
            <div class="d-flex align-center pa-3">
              <div class="flex-grow-1">
                <div class="text-body-1">
                  {{ h.name }}
                  <v-chip v-if="h.id === activeId" size="x-small" color="teal" variant="tonal" class="ml-2">{{ t('home.active') }}</v-chip>
                </div>
                <div class="text-caption text-medium-emphasis">{{ h.layout + 1 }} {{ t('home.keys') }}</div>
              </div>
              <v-btn
                v-if="h.id !== activeId"
                size="small" variant="tonal" color="teal" class="mr-2"
                :loading="activatingId === h.id"
                @click="doActivate(h.id)"
              >{{ t('home.activate') }}</v-btn>
              <v-btn icon="delete" size="small" variant="text" color="error" @click="askDelete(h)"></v-btn>
            </div>

            <!-- 键位绑定编辑区 -->
            <v-expand-transition>
              <div v-if="expandedId === h.id" class="px-3 pb-3">
                <div class="manage-grid">
                  <div v-for="key in h.layout + 1" :key="key" class="key-edit">
                    <div class="text-caption text-medium-emphasis mb-1">{{ t('home.keyN', { n: key }) }}</div>
                    <v-select
                      :model-value="h.slots[key - 1] || 0"
                      :items="signalItems"
                      variant="outlined"
                      density="compact"
                      hide-details
                      @update:model-value="v => onSlotChange(h, key, v)"
                    ></v-select>
                  </div>
                </div>
              </div>
            </v-expand-transition>

            <v-divider></v-divider>
            <v-card-actions class="py-1">
              <v-btn
                variant="text"
                size="small"
                :icon="expandedId === h.id ? 'expand_less' : 'expand_more'"
                @click="expandedId = (expandedId === h.id ? null : h.id)"
              >{{ t('home.bindTitle') }}</v-btn>
            </v-card-actions>
          </v-card>
        </template>

        <v-card v-else variant="outlined" class="text-center pa-6">
          <p class="text-caption text-medium-emphasis">{{ t('home.noProfiles') }}</p>
        </v-card>
      </v-card-text>

      <v-divider></v-divider>
      <v-card-actions>
        <v-spacer></v-spacer>
        <v-btn variant="plain" @click="$emit('update:modelValue', false)">{{ t('common.close') }}</v-btn>
      </v-card-actions>

      <!-- 删除主页确认 -->
      <ConfirmDialog
        v-model="deleteDialog"
        :title="t('home.deleteTitle')"
        :message="t('home.deleteMsg', { name: deleteTarget ? deleteTarget.name : '' })"
        :confirm-text="t('common.delete')"
        :loading="deleting"
        @confirm="doDelete"
      ></ConfirmDialog>
    </v-card>
  </v-dialog>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAppStore } from '@/stores/app'
import { listHomes, createHome, saveHome, deleteHome, activateHome } from '@/api/home'
import ConfirmDialog from '@/components/ConfirmDialog.vue'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
  signals: { type: Array, default: () => [] },
})

const emit = defineEmits(['update:modelValue', 'changed'])

const { t } = useI18n()
const app = useAppStore()

const homes = ref([])
const activeId = ref(null)
const expandedId = ref(null)

const createOpen = ref(false)
const createName = ref('')
const createLayout = ref(8)   // 默认9键
const creating = ref(false)

const activatingId = ref(null)
const deleteDialog = ref(false)
const deleteTarget = ref(null)
const deleting = ref(false)

watch(() => props.modelValue, (open) => { if (open) load() })

async function load () {
  try {
    const data = await listHomes({ _silent: true })
    homes.value = data.profiles || []
    activeId.value = data.active
  } catch (e) {
    /* 拦截器已提示 */
  }
}

const layoutItems = computed(() => {
  const items = []
  for (let n = 1; n <= 9; n++) {
    items.push({ title: `${n} ${t('home.keys')}`, value: n - 1 })
  }
  return items
})

// 信号选择项：0=清除绑定
const signalItems = computed(() => {
  const items = [{ title: `— ${t('home.unbind')} —`, value: 0 }]
  props.signals.forEach(s => {
    items.push({ title: `${s.kind} #${s.index} ${s.name || t('common.unnamed')}`, value: s.index })
  })
  return items
})

async function doCreate () {
  creating.value = true
  try {
    const data = await createHome(createName.value, createLayout.value)
    app.notify(t('home.created'))
    createOpen.value = false
    createName.value = ''
    await load()
    if (typeof data.id === 'number') expandedId.value = data.id
    emit('changed')
  } catch (e) {
    /* 拦截器已提示（满额等） */
  } finally {
    creating.value = false
  }
}

async function doActivate (id) {
  activatingId.value = id
  try {
    await activateHome(id)
    app.notify(t('home.activated'))
    activeId.value = id
    emit('changed')
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    activatingId.value = null
  }
}

function askDelete (h) {
  deleteTarget.value = h
  deleteDialog.value = true
}

async function doDelete () {
  deleting.value = true
  try {
    await deleteHome(deleteTarget.value.id)
    app.notify(t('home.deleted'))
    deleteDialog.value = false
    if (expandedId.value === deleteTarget.value.id) expandedId.value = null
    // 删除最后一个主页时固件自动重建默认主页
    await load()
    emit('changed')
  } catch (e) {
    /* 拦截器已提示 */
  } finally {
    deleting.value = false
  }
}

// 键位绑定变更：实时保存该主页
async function onSlotChange (profile, key, value) {
  const slots = [...profile.slots]
  slots[key - 1] = value || 0
  try {
    await saveHome({ id: profile.id, name: profile.name, layout: profile.layout, slots })
    profile.slots = slots
    app.notify(t('home.saved'))
    emit('changed')
  } catch (e) {
    /* 拦截器已提示 */
  }
}
</script>

<style scoped>
.manage-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
}
</style>
