<template>
  <div>
    <el-page-header content="Сервисные обращения" />
    <el-button type="primary" style="margin: 12px 0" @click="createOpen = true">
      Новая заявка
    </el-button>
    <el-table :data="items" stripe>
      <el-table-column prop="_id" label="ID" width="220" />
      <el-table-column prop="state" label="Стадия" width="160" />
      <el-table-column label="Тема">
        <template #default="{ row }">{{ String(row.fields?.title ?? '—') }}</template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="createOpen" title="Новая заявка">
      <el-form :model="draft" label-width="120px">
        <el-form-item label="Тема"><el-input v-model="draft.title" /></el-form-item>
        <el-form-item label="Категория"><el-input v-model="draft.category" /></el-form-item>
        <el-form-item label="Приоритет">
          <el-select v-model="draft.priority">
            <el-option label="low" value="low" />
            <el-option label="normal" value="normal" />
            <el-option label="high" value="high" />
            <el-option label="urgent" value="urgent" />
          </el-select>
        </el-form-item>
        <el-form-item label="Описание"><el-input v-model="draft.description" type="textarea" /></el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="createOpen = false">Отмена</el-button>
        <el-button type="primary" @click="create">Создать</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { api, type ServiceRequest } from '../api'

const items = ref<ServiceRequest[]>([])
const createOpen = ref(false)
const draft = reactive({ title: '', category: '', priority: 'normal', description: '' })

async function load() {
  const { data } = await api.get('/api/v1/requests', { params: { limit: 50 } })
  items.value = data.items ?? []
}

async function create() {
  await api.post('/api/v1/requests', {
    templateKey: 'service_request',
    fields: { ...draft },
  })
  createOpen.value = false
  await load()
}

onMounted(load)
</script>
