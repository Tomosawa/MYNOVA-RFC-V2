<template>
  <div>
    <!-- 工具栏 -->
    <v-row dense class="mb-1">
      <v-col cols="8">
        <v-text-field
          v-model="search"
          :label="t('common.search')"
          prepend-inner-icon="search"
          variant="outlined"
          density="compact"
          clearable
          hide-details
        ></v-text-field>
      </v-col>
      <v-col cols="4" class="text-right">
        <v-btn color="orange" variant="tonal" size="small" prepend-icon="settings_remote" @click="$emit('learn')">
          {{ t('learn.btn') }}
        </v-btn>
      </v-col>
    </v-row>

    <!-- 列表 -->
    <v-list v-if="filtered.length > 0" class="py-0" border>
      <template v-for="item in displayed" :key="item.index">
        <v-list-item>
          <template #prepend>
            <v-avatar color="orange" size="36">
              <span class="text-body-1">{{ item.index - 100 }}</span>
            </v-avatar>
          </template>

          <v-list-item-title>{{ item.name || t('common.unnamed') }}</v-list-item-title>
          <v-list-item-subtitle>
            {{ item.protocolName || 'RAW' }} | {{ t('signals.frames') }} {{ item.frameCount }} | {{ t('signals.edges') }} {{ item.totalEdges }}
          </v-list-item-subtitle>

          <template #append>
            <v-btn icon="visibility" size="small" variant="text" @click="$emit('detail', item)"></v-btn>
            <v-btn icon="edit" size="small" variant="text" @click="$emit('edit', item)"></v-btn>
            <v-btn icon="delete" size="small" variant="text" color="error" @click="$emit('delete', item)"></v-btn>
          </template>
        </v-list-item>
        <v-divider></v-divider>
      </template>
    </v-list>

    <v-card v-else variant="outlined" class="text-center pa-6">
      <v-icon size="56" color="grey">settings_remote</v-icon>
      <p class="text-grey mt-2 mb-1">{{ t('signals.noData') }}</p>
      <p class="text-caption text-grey">{{ t('signals.noIrHint') }}</p>
    </v-card>

    <!-- 渐进渲染：大数据量时减少初始DOM，打开对话框更流畅 -->
    <div v-if="hasMore" class="text-center mt-3">
      <v-btn variant="outlined" size="small" @click="loadMore">{{ t('common.loadMore') }}</v-btn>
    </div>
  </div>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'

const props = defineProps({
  signals: { type: Array, default: () => [] },
})

defineEmits(['learn', 'edit', 'detail', 'delete'])

const { t } = useI18n()
const search = ref('')

const PAGE_SIZE = 30
const page = ref(1)

const filtered = computed(() => {
  let list = props.signals.filter(i => i.kind === 'IR')
  if (search.value) {
    const q = search.value.toLowerCase()
    list = list.filter(i =>
      (i.name || '').toLowerCase().includes(q) ||
      String(i.index - 100).includes(q) ||
      (i.protocolName || '').toLowerCase().includes(q))
  }
  return list
})

const displayed = computed(() => filtered.value.slice(0, page.value * PAGE_SIZE))
const hasMore = computed(() => displayed.value.length < filtered.value.length)

function loadMore () {
  page.value++
}

// 搜索/数据变化时回到第一页
watch([search, () => props.signals], () => { page.value = 1 })
</script>
