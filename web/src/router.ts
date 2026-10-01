import { createRouter, createWebHistory } from 'vue-router'
import Login from './views/Login.vue'
import Requests from './views/Requests.vue'
import RequestDetail from './views/RequestDetail.vue'
import Guest from './views/Guest.vue'
import { tenantStore } from './api'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/login', component: Login },
    { path: '/', redirect: '/requests' },
    { path: '/requests', component: Requests },
    { path: '/requests/:id', component: RequestDetail, props: true },
    { path: '/g/:token', component: Guest, props: true },
  ],
})

router.beforeEach((to) => {
  if (to.path.startsWith('/g/')) return true // гостевой контур без логина
  if (!tenantStore.token && to.path !== '/login') return '/login'
  return true
})

export default router
