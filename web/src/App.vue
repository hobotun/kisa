<template>
  <el-container style="min-height: 100vh">
    <el-header>
      <span>КИСА</span>
      <span v-if="tenant" style="margin-left: 12px; color: #909399">{{ tenant }}</span>
    </el-header>
    <el-main><router-view /></el-main>
  </el-container>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { tenantStore } from './api'
const tenant = computed(() => tenantStore.slug)
</script>
