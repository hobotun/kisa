import axios from 'axios'

// Тенант: поддомен (acme.host) приоритетнее, иначе поле логина.
// Токены храним в разрезе тенанта: kisa:<slug>:access
const tenantFromHost = () => {
  const h = window.location.hostname.split('.')
  return h.length > 2 ? h[0] : ''
}

export const tenantStore = {
  get slug() {
    return sessionStorage.getItem('kisa:tenant') || tenantFromHost()
  },
  set slug(v: string) {
    sessionStorage.setItem('kisa:tenant', v)
  },
  tokenKey() {
    return `kisa:${this.slug}:access`
  },
  get token() {
    return localStorage.getItem(this.tokenKey()) || ''
  },
  set token(v: string) {
    if (v) localStorage.setItem(this.tokenKey(), v)
    else localStorage.removeItem(this.tokenKey())
  },
}

export const api = axios.create({ baseURL: '/' })

api.interceptors.request.use((cfg) => {
  const h = cfg.headers as unknown as {
    set?: (k: string, v: string) => void
    [k: string]: unknown
  }
  if (tenantStore.slug) {
    if (h.set) h.set('X-Tenant', tenantStore.slug)
    else h['X-Tenant'] = tenantStore.slug
  }
  if (tenantStore.token) {
    if (h.set) h.set('Authorization', `Bearer ${tenantStore.token}`)
    else h['Authorization'] = `Bearer ${tenantStore.token}`
  }
  return cfg
})

export interface AllowedAction {
  action: string
  title: string
  needsComment?: boolean
  requiredFields?: string[]
}

export interface ServiceRequest {
  _id: string
  state: string
  fields: Record<string, unknown>
  allowedActions: AllowedAction[]
}
