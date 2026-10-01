<template>
  <el-card v-if="card" style="max-width: 560px; margin: 40px auto">
    <template #header>Заявка · гостевой доступ</template>
    <p>{{ card.title }}</p>
    <p style="color: #909399">{{ card.state }}</p>
    <el-button
      v-for="a in card.allowedActions"
      :key="a.action"
      type="primary"
      @click="run(a.action)"
    >
      {{ a.title }}
    </el-button>
  </el-card>
</template>

<script setup lang="ts">
import axios from 'axios'
import { onMounted, ref } from 'vue'

const props = defineProps<{ token: string }>()
const card = ref<{ title: string; state: string; allowedActions: { action: string; title: string }[] } | null>(null)

async function load() {
  const { data } = await axios.get(`/g/${props.token}`)
  card.value = data
}

async function run(action: string) {
  await axios.post(`/g/${props.token}/transitions`, { action })
  await load() // после confirm action гаснет, карточка read-only до TTL
}

onMounted(load)
</script>
