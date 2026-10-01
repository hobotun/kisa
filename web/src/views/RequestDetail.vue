<template>
  <div v-if="item">
    <el-page-header content="Заявка" @back="$router.push('/requests')" />
    <h3>{{ String(item.fields?.title ?? item._id) }} · {{ item.state }}</h3>
    <!-- Кнопки ТОЛЬКО из allowedActions с сервера, стадий в коде нет -->
    <div style="margin: 12px 0">
      <el-button
        v-for="a in item.allowedActions"
        :key="a.action"
        type="primary"
        plain
        @click="run(a.action, a.needsComment)"
      >
        {{ a.title }}
      </el-button>
    </div>
    <el-dialog v-model="commentOpen" title="Комментарий">
      <el-input v-model="comment" type="textarea" />
      <template #footer>
        <el-button @click="commentOpen = false">Отмена</el-button>
        <el-button type="primary" @click="send">Отправить</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api, type ServiceRequest } from '../api'

const props = defineProps<{ id: string }>()
const item = ref<ServiceRequest | null>(null)
const commentOpen = ref(false)
const comment = ref('')
let pendingAction = ''

async function load() {
  const { data } = await api.get(`/api/v1/requests/${props.id}`)
  item.value = data
}

function run(action: string, needsComment?: boolean) {
  pendingAction = action
  if (needsComment) {
    comment.value = ''
    commentOpen.value = true
  } else {
    send()
  }
}

async function send() {
  commentOpen.value = false
  const { data } = await api.post(`/api/v1/requests/${props.id}/transitions`, {
    action: pendingAction,
    comment: comment.value || undefined,
  })
  item.value = data
}

onMounted(load)
</script>
