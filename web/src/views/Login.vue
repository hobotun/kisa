<template>
  <el-card style="max-width: 420px; margin: 60px auto">
    <template #header>Вход · КИСА</template>
    <el-form :model="form" label-width="110px" @submit.prevent="login">
      <el-form-item label="Тенант">
        <el-input v-model="form.tenant" placeholder="acme" />
      </el-form-item>
      <el-form-item label="Логин">
        <el-input v-model="form.login" />
      </el-form-item>
      <el-form-item label="Пароль">
        <el-input v-model="form.password" type="password" />
      </el-form-item>
      <el-form-item>
        <el-button type="primary" :loading="loading" @click="login">Войти</el-button>
      </el-form-item>
      <el-alert v-if="error" :title="error" type="error" :closable="false" />
    </el-form>
  </el-card>
</template>

<script setup lang="ts">
import { reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { api, tenantStore } from '../api'

const router = useRouter()
const loading = ref(false)
const error = ref('')
const form = reactive({ tenant: tenantStore.slug, login: '', password: '' })

async function login() {
  loading.value = true
  error.value = ''
  try {
    tenantStore.slug = form.tenant.trim()
    const { data } = await api.post('/api/v1/auth/login', {
      tenantSlug: form.tenant.trim(),
      login: form.login,
      password: form.password,
      device: navigator.userAgent.slice(0, 120),
    })
    tenantStore.token = data.accessToken
    tenantStore.refresh = data.refreshToken
    router.push('/requests')
  } catch (e: unknown) {
    const msg =
      (e as { response?: { data?: { error?: { message?: string } } } })?.response?.data
        ?.error?.message || 'Сервер вернул ошибку'
    error.value = msg
  } finally {
    loading.value = false
  }
}
</script>
