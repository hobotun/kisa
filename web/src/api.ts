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
  refreshKey() {
    return `kisa:${this.slug}:refresh`
  },
  get token() {
    return localStorage.getItem(this.tokenKey()) || ''
  },
  set token(v: string) {
    if (v) localStorage.setItem(this.tokenKey(), v)
    else localStorage.removeItem(this.tokenKey())
  },
  get refresh() {
    return localStorage.getItem(this.refreshKey()) || ''
  },
  set refresh(v: string) {
    if (v) localStorage.setItem(this.refreshKey(), v)
    else localStorage.removeItem(this.refreshKey())
  },
  clear() {
    localStorage.removeItem(this.tokenKey())
    localStorage.removeItem(this.refreshKey())
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

api.interceptors.response.use(
  (r) => r,
  async (err) => {
    const cfg = err.config as (typeof err.config & { _retried?: boolean }) | undefined
    const status = err.response?.status
    const url: string = cfg?.url ?? ''
    // refresh ротируемый и одноразовый: при 401 пробуем один раз, иначе на логин
    if (status === 401 && cfg && !cfg._retried && !url.includes('/auth/') && tenantStore.refresh) {
      cfg._retried = true
      try {
        const { data } = await axios.post('/api/v1/auth/refresh', {
          refreshToken: tenantStore.refresh,
        })
        tenantStore.token = data.accessToken
        tenantStore.refresh = data.refreshToken
        return api(cfg)
      } catch {
        tenantStore.clear()
        if (!window.location.pathname.startsWith('/g/')) window.location.href = '/login'
      }
    }
    return Promise.reject(err)
  },
)

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
