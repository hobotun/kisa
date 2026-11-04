# KISA — Архитектура и документация

## Обзор

KISA — многотенантная документоориентированная информационная система общего назначения. Тенант настраивает произвольные типы документов, workflow, права доступа и рабочие места без изменения кода. `service_request` — встроенный пример, демонстрирующий возможности платформы.

## Стек

| Компонент | Технологии |
|-----------|-----------|
| **Backend (server)** | C++20, Qt 6 (QHttpServer), MongoDB (mongocxx 3.x), Argon2id |
| **Shared library (lib)** | Qt 6, C++ — API-клиент, DTO, TenantSession |
| **Frontend (web)** | Vue 3 + TypeScript, Vue Router 4, Element Plus, Axios, Vite |
| **Desktop (win-app)** | Qt 6 Widgets, C++ |
| **Deploy** | Docker Compose, nginx, Caddy/Traefik (reverse proxy) |
| **CI/CD** | GitHub Actions → GHCR → Portainer webhook |

---

## Архитектура компонентов

```
┌──────────────────────────────────────────────────────┐
│                   Reverse Proxy (Caddy)              │
│          kisa.example.com / acme.kisa.example.com    │
└──────────────┬────────────────────┬──────────────────┘
               │                    │
          :8081 (web)          :8080 (server)
               │                    │
┌──────────────▼──────┐   ┌─────────▼────────────────┐
│   web (Vue 3 SPA)   │   │  server (Qt HttpServer)  │
│   nginx + статика   │   │                          │
└─────────────────────┘   │  registerAuth()          │
                           │  registerMeta()          │
┌────────────────────┐    │  registerEngine()        │
│  win-app (Widgets) │───▶│                          │
└────────────────────┘    └─────────────┬────────────┘
         │                              │
  kisa-lib (shared)              ┌──────▼──────┐
  KisaClient                     │  MongoDB 7  │
  TenantSession                  │  (internal) │
  DTO                            └─────────────┘
```

**Сервер** разбит на три регистрационных слоя:
- `registerAuth` — аутентификация, сессии
- `registerMeta` — CRUD конфигурации (шаблоны, роли, подразделения)
- `registerEngine` — движок документов и workflow

Тенант определяется заголовком `X-Tenant` (slug). `tenantId` в тело запроса не принимается.

---

## API

### Auth — `/api/v1/auth/*`

| Метод | Путь | Тело / Параметры | Ответ |
|-------|------|------------------|-------|
| POST | `/api/v1/auth/resolve` | header `X-Tenant` | `{slug, name}` |
| POST | `/api/v1/auth/login` | `{login, password, device?}` | `{accessToken, refreshToken, expiresIn, user:{login,name,roles}}` |
| POST | `/api/v1/auth/refresh` | `{refreshToken}` | те же поля, старая сессия удаляется |
| POST | `/api/v1/auth/logout` | header `Authorization` | `{}` |
| GET | `/api/v1/me` | — | `{login, name, roles, tenantId}` |

### Meta — `/api/v1/admin/*` (только `tenant_admin`)

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/api/v1/admin/doc-templates` | Список `?status=&limit=&page=` |
| POST | `/api/v1/admin/doc-templates` | Создать (v1, status=draft) |
| GET | `/api/v1/admin/doc-templates/<key>` | Все версии шаблона |
| POST | `/api/v1/admin/doc-templates/<key>/versions/<ver>/publish` | Опубликовать |
| GET | `/api/v1/admin/workflow-templates` | Список |
| POST | `/api/v1/admin/workflow-templates` | Создать |
| GET | `/api/v1/admin/workflow-templates/<key>` | Все версии |
| POST | `/api/v1/admin/workflow-templates/<key>/versions/<ver>/publish` | Опубликовать |
| GET | `/api/v1/admin/roles` | Список ролей тенанта |
| GET | `/api/v1/admin/branches` | Список подразделений |

### Engine — `/api/v1/requests`, `/api/v1/documents`

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/api/v1/requests` | Создать заявку + автоматический переход `submit` |
| GET | `/api/v1/requests` | Список `?state=&priority=&overdue=1&mine=1` |
| GET | `/api/v1/requests/<id>` | Деталь + `allowedActions` + `history` |
| POST | `/api/v1/requests/<id>/transitions` | Выполнить переход `{action, comment?, onBehalfOf?, fields{}, assignees[]}` |
| POST | `/api/v1/requests/<id>/mode` | Установить `approvalMode: proxy\|guest` |
| POST | `/api/v1/requests/<id>/comments` | Добавить комментарий `{text}` |
| GET | `/g/<token>` | Гостевая карточка (без авторизации) |
| POST | `/g/<token>/transitions` | Гостевой переход (одноразовый) |
| GET | `/api/v1/workflows?kind=<key>` | Опубликованный workflow-шаблон |
| POST | `/api/v1/documents` | Создать документ с автономером |
| GET | `/api/v1/documents` | Список `?templateKey=&limit=` |
| GET | `/api/v1/documents/<id>` | Деталь документа |

### Системные

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/healthz` | `{"status":"ok"}` |
| GET | `/openapi.yaml` | Заглушка OpenAPI 3.0.3 |

---

## Схема данных MongoDB

### tenants
```
_id:       ObjectId
slug:      string        -- unique index
name:      string
status:    string        -- "active"
createdAt: Date
```

### users
```
_id:          ObjectId
tenantId:     ObjectId   -- index: {tenantId, login} unique
login:        string
passwordHash: string     -- argon2id encoded
name:         string
roles:        string[]
status:       string     -- "active"
createdAt:    Date
```

### sessions
```
_id:         ObjectId
tenantId:    ObjectId
userId:      ObjectId
accessHash:  string      -- SHA-256(token), unique index
refreshHash: string      -- SHA-256(token), unique index
accessExp:   Date
refreshExp:  Date        -- TTL index, MongoDB автоудаляет
device:      string
createdAt:   Date
```

### roles
```
_id:               ObjectId
tenantId:          ObjectId
roleKey:           string
title:             string
canAdminUsers:     bool?
canAdminTemplates: bool?
```

### workflow_templates
```
_id:       ObjectId
tenantId:  ObjectId     -- index: {tenantId, key, version} unique
key:       string       -- [a-z0-9_-], max 64
version:   int32
status:    string       -- "draft" | "published"
title:     string
states: [{
  key:       string
  title:     string
  terminal?: bool
}]
transitions: [{
  from:           string
  to:             string
  action:         {key: string, title: string}
  roles:          string[]
  requiredFields: string[]?
  guestScope:     string[]?
  needsComment:   bool?
  countAsReopen:  bool?
  guards?: {
    separationFrom: string   -- ключ action, автор которого не может выполнить этот
    proxyAllowed:   bool
  }
}]
slaByPriority: {low, normal, high, urgent}  -- часы (int32)
reopenLimit:   int32
approvalModes: string[]
guestTtlHours: int32
instruction:   string?
createdBy:     ObjectId
createdAt:     Date
```

### doc_templates
```
_id:      ObjectId
tenantId: ObjectId     -- index: {tenantId, key, version} unique
key:      string
version:  int32
status:   string       -- "draft" | "published"
title:    string
fields:   [{key: string, required?: bool, ...}]
numbering?:      any
slaByPriority?:  any
instruction?:    any
createdBy: ObjectId
createdAt: Date
```

### requests
```
_id:             ObjectId
tenantId:        ObjectId
templateKey:     string
templateVersion: int32
state:           string
fields: {
  title:       string
  category:    string?
  priority:    string     -- "low"|"normal"|"high"|"urgent"
  description: string
  solution?:   string
  estimate?:   any
  report?:     string
  ...           -- произвольные поля шаблона
}
requesterId:  ObjectId
assigneeIds:  ObjectId[]
approvalMode: string      -- "proxy"|"guest"
branchId:     string?
counterpartyId: string?
dueAt:        Date        -- now + slaByPriority[priority]
reopenCount:  int32
createdBy:    ObjectId
createdAt:    Date
updatedAt:    Date
```

### request_events (append-only лог)
```
_id:       ObjectId
tenantId:  ObjectId
requestId: ObjectId
ts:        Date
actor: {
  type:    string   -- "user" | "guest"
  label:   string
  userId?: ObjectId
}
from:      string
to:        string
action:    string
comment?:  string
onBehalfOf?: string
```

### request_comments
```
_id:         ObjectId
tenantId:    ObjectId
requestId:   ObjectId
authorId:    ObjectId
authorType:  string     -- "user"
authorLabel: string
text:        string
createdAt:   Date
```

### documents
```
_id:             ObjectId
tenantId:        ObjectId
templateKey:     string
templateVersion: int32
number:          string    -- "<key>-y<year>-<seq>"
fields:          object    -- валидируется по doc_template.fields[].required
branchId:        string?
status:          string    -- "active"
createdBy:       ObjectId
createdAt:       Date
updatedAt:       Date
```

### counters
```
_id:      ObjectId
tenantId: ObjectId
key:      string     -- templateKey
period:   string     -- "y2025", "y2026", ...
seq:      int32      -- инкремент через $inc + upsert (atomic)
```

### guest_tokens
```
_id:         ObjectId
tenantId:    ObjectId
requestId:   ObjectId
scope:       string[]   -- список разрешённых action keys
tokenHash:   string     -- SHA-256(token)
expiresAt:   Date       -- now + guestTtlHours (default 72ч)
usedActions: string[]   -- $push при каждом использовании (одноразовость)
createdBy:   ObjectId
createdAt:   Date
```

---

## Бизнес-логика

### Аутентификация

- **Токены**: opaque hex 256 бит. В БД хранится только `SHA-256(token)`.
- **Access TTL**: 15 минут. **Refresh TTL**: 30 дней (MongoDB TTL-index).
- **Refresh**: одноразовый — старая сессия удаляется, выдаётся новая.
- **Пароли**: argon2id (t=3, m=65536 KB, p=1, salt 16 байт, hash 32 байта).
- **Principal**: при каждом запросе — lookup по `SHA-256(Bearer token)`, проверка `accessExp`, совпадение `tenantId` с `X-Tenant`, статус пользователя `active`.
- **Rate-limit**: in-memory, ключ `tenantOid/login`. 5 неудач → блокировка 5 минут.
- **Видимость заявок**: `dispatcher` и `tenant_admin` видят все. Остальные — только где `requesterId=me` или `assigneeIds∋me`.

### Workflow state machine

Состояние заявки и разрешённые переходы полностью определяются `workflow_template`.

**Guards**:
- `separationFrom` — SOD (Separation of Duties): автор указанного action не может выполнить текущий. Обход через `onBehalfOf` при `proxyAllowed=true`.

**SLA**: при создании заявки вычисляется `dueAt = now + slaByPriority[priority]`. Поле `overdue` вычисляется на лету: `dueAt < now AND state не terminal`.

**Reopen**: каждый переход с `countAsReopen=true` инкрементирует `reopenCount`. Если `reopenCount >= reopenLimit` — переход блокируется.

**Guest mode**: при установке `approvalMode=guest` создаётся `guest_token` со `scope` = объединение `guestScope` всех переходов. Каждое действие одноразовое (`usedActions`). Токен: opaque hex → SHA-256 в БД.

### Встроенный шаблон `service_request` (v1)

```
draft → triage → proposal → approval → execution → confirmation → archived
                                                                   ^^^^ terminal
```

| Переход | action | Роли | requiredFields | Примечания |
|---------|--------|------|----------------|------------|
| draft → triage | submit | requester | — | |
| triage → proposal | accept_triage | dispatcher | — | |
| triage → archived | reject_triage | dispatcher | — | needsComment |
| triage → draft | return_triage | dispatcher | — | needsComment |
| proposal → approval | propose | analyst | solution, estimate | |
| approval → execution | approve | approver | — | guestScope; SOD от propose |
| approval → proposal | reject_approval | approver | — | needsComment |
| execution → execution | assign | dispatcher, tenant_admin | — | |
| execution → confirmation | resolve | specialist | report | |
| confirmation → archived | confirm | requester | — | guestScope |
| confirmation → execution | reopen | requester | — | countAsReopen |

SLA: urgent=4ч, high=8ч, normal=24ч, low=72ч. Reopen limit: 2.

### Автономера документов

Формат: `<templateKey>-y<year>-<seq>`, например `service_request-y2026-0042`.  
Счётчик атомарен: `find_one_and_update` с `$inc` + `upsert` по `{tenantId, key, period}`.

---

## Shared Library (lib/)

Общий Qt/C++ код для `server` и `win-app`.

### KisaClient

`QObject`-обёртка над `QNetworkAccessManager`. Базовый URL из env `KISA_URL`.

| Метод | Сигнал результата |
|-------|-------------------|
| `login(login, password, device?)` | `loginDone(UserProfile)` / `loginFailed(code, msg)` |
| `me()` | `meDone(UserProfile)` |
| `getRequests(QUrlQuery?)` | `requestsDone(QList<ServiceRequest>)` |
| `getRequest(id)` | `requestDone(ServiceRequest)` |
| `postTransition(id, action, comment?, payload?)` | `transitionDone(ServiceRequest)` |
| `postComment(id, text)` | — |
| `logout()` | — (POST + `session.clear()`) |

Автоматический refresh: при 401 пробует обновить токен, повторяет запрос. При неудаче — `networkError`.

### TenantSession

`QObject`. Хранит `slug`, `accessToken`, `refreshToken`. Персистирует через `QSettings` (группа `session/<slug>`). `clear()` → сигнал `cleared()`.

### DTO

| Структура | Поля |
|-----------|------|
| `AllowedAction` | action, title, needsComment, requiredFields |
| `HistoryEvent` | id, from, to, action, comment, ts, actorType, actorLabel |
| `ServiceRequest` | id, state, stateTitle, fields (QJsonObject), allowedActions, history, overdue, dueAt, createdAt, updatedAt; хелперы `title()`, `priority()` |
| `UserProfile` | login, name, roles, tenantId; хелперы `hasRole()`, `isPrivileged()` |

Все структуры: статический `fromJson()`. Даты из Extended JSON (`$date.$numberLong`).

---

## Win-App (Qt Widgets)

Нативное Windows-приложение. `baseUrl` из env `KISA_URL` (default: `https://kisa.muravyev.online`).

### Окна и виджеты

**LoginWindow** (QDialog, 340px)
- Поля: Тенант, Логин, Пароль
- Кнопка «Войти» (блокируется на время запроса), лейбл ошибки

**MainWindow** (QMainWindow, 1100×680)
- Меню Файл: «Выйти из системы», «Закрыть»
- Левая панель: навигация (QListWidget, 160px) — пункт «Обращения»
- Правая часть: QSplitter → RequestsWidget + RequestDetailWidget (2:1)
- Статус-бар: `name @ slug`

**RequestsWidget**
- Тулбар: «Обновить» (кнопка «Новая заявка» — TODO)
- QTableWidget: Тема, Стадия, Приоритет, Срок
- Просроченные строки — красный фон
- Двойной клик → `emit requestSelected(id)`

**RequestDetailWidget**
- Заголовок (14pt bold), стадия
- Кнопки переходов из `allowedActions` (динамически перестраиваются)
- При `needsComment` — `QInputDialog::getMultiLineText`
- История (QTableWidget): Время, Действие, Из, В, Кто, Комментарий

### Поток управления

```
App start
  ├─ session exists → client.me() → MainWindow.show()
  └─ no session     → LoginWindow.show()

session.cleared() → MainWindow.hide() → LoginWindow.show()
```

---

## Web (Vue 3)

SPA, тенант определяется по поддомену (`acme.kisa.example.com → slug="acme"`) или из `sessionStorage`.

### Роутер

| Путь | Компонент | Auth |
|------|-----------|------|
| `/login` | Login.vue | нет |
| `/` | redirect → `/requests` | да |
| `/requests` | Requests.vue | да |
| `/requests/:id` | RequestDetail.vue | да |
| `/g/:token` | Guest.vue | нет |

Guard: `!tenantStore.token` → redirect `/login` (кроме `/g/*`).

### Компоненты

**Login.vue** — форма (тенант + логин + пароль), ElCard 420px, ошибка.

**Requests.vue** — `el-table` (ID, Стадия, Тема), диалог создания новой заявки (title, category, priority, description; `templateKey=service_request` хардкодирован).

**RequestDetail.vue** — заголовок, стадия, кнопки из `allowedActions`, диалог комментария при `needsComment`, обновление заявки на месте после перехода.

**Guest.vue** — карточка без авторизации: title, state, кнопки действий. После использования action кнопка исчезает (одноразовость). Перечитывает карточку после каждого действия.

### Axios interceptors

- **Request**: `X-Tenant: <slug>`, `Authorization: Bearer <accessToken>`
- **Response 401**: автоматический refresh → повтор запроса. При неудаче: `clear()` + redirect `/login`.
- Токены: `localStorage('kisa:<slug>:access')` / `...refresh`.

---

## Deploy

### docker-compose.yml

| Сервис | Образ | Порт (bind) | Примечания |
|--------|-------|-------------|------------|
| `mongo` | mongo:7 | — (внутренний) | volume `mongo-data`, healthcheck через mongosh |
| `server` | `ghcr.io/hobotun/kisa-server:latest` | `127.0.0.1:8080` | env: MONGO_URI, PORT, SEED_* |
| `web` | `ghcr.io/hobotun/kisa-web:latest` | `127.0.0.1:8081` | nginx + статика Vue |

Сети: внутренняя сеть между сервисами + внешняя сеть `proxy` (Caddy/Traefik на хосте).

### Env-переменные сервера

| Переменная | Назначение |
|-----------|-----------|
| `MONGO_URI` | URI подключения к MongoDB |
| `PORT` | HTTP-порт сервера (default 8080) |
| `SEED_TENANT_SLUG` | Slug первого тенанта (default `demo`) |
| `SEED_ADMIN_LOGIN` | Логин первого admin (default `admin`) |
| `SEED_ADMIN_PASSWORD` | Пароль первого admin (обязателен для seed) |

### Seed (первый старт)

Если коллекция `tenants` пуста и `SEED_ADMIN_PASSWORD` задан:
1. Создаётся тенант с указанным slug
2. Создаётся 5 базовых ролей: `requester`, `dispatcher`, `analyst`, `approver`, `specialist`, `tenant_admin`
3. Создаётся пользователь-администратор
4. Публикуется workflow-шаблон `service_request` v1

### Индексы MongoDB (создаются при старте)

| Коллекция | Индекс | Флаги |
|-----------|--------|-------|
| `tenants` | `slug` | unique |
| `users` | `{tenantId, login}` | unique |
| `sessions` | `accessHash` | unique |
| `sessions` | `refreshHash` | unique |
| `sessions` | `refreshExp` | TTL |
| `doc_templates` | `{tenantId, key, version}` | unique |
| `workflow_templates` | `{tenantId, key, version}` | unique |

### CI/CD

GitHub Actions (`.github/workflows/`): на каждый push в `main` — сборка Docker-образа через `docker/build-push-action`, публикация в GHCR, деплой через Portainer webhook.

---

## Настройка рабочего места (новая машина)

### 1. Клонировать репозиторий

```bash
git clone git@github.com:hobotun/kisa.git
```

### 2. Win-app (Qt Widgets)

Установить:
- **Qt 6.12.0** (MinGW 64-bit) — Qt Online Installer → `C:/Qt/6.12.0/`
- **MinGW64** — `C:/dev/mingw64/` (или взять из комплекта Qt)
- **CMake ≥ 3.21** — добавить в PATH

Создать `win-app/CMakeUserPresets.json` вручную (файл в `.gitignore`, машиноспецифичный):

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "win-debug",
      "inherits": "windows-mingw-debug",
      "cacheVariables": {
        "Qt6_DIR": "C:/Qt/6.12.0/mingw_64/lib/cmake/Qt6",
        "CMAKE_MAKE_PROGRAM": "C:/Qt/Tools/Ninja/ninja.exe"
      }
    }
  ],
  "buildPresets": [
    { "name": "win-debug", "configurePreset": "win-debug" }
  ]
}
```

Собрать:

```bash
cd win-app
cmake --preset win-debug
cmake --build --preset win-debug
```

Скопировать Qt DLL рядом с `.exe` (нужно один раз):

```bash
C:\Qt\6.12.0\mingw_64\bin\windeployqt.exe build\win-debug\kisa-win.exe
```

### 3. Сервер (Docker)

```bash
# в корне проекта
docker compose up -d
```

Переменные окружения (передать через `.env` или `docker-compose.override.yml`):

| Переменная | Пример |
|-----------|--------|
| `MONGO_URI` | `mongodb://mongo:27017` |
| `PORT` | `8080` |
| `SEED_TENANT_SLUG` | `demo` |
| `SEED_ADMIN_LOGIN` | `admin` |
| `SEED_ADMIN_PASSWORD` | `changeme` |

### 4. Веб (Vue 3)

```bash
cd web
npm install
npm run dev
```

---

## Что не реализовано (TODO)

- Кнопка «Новая заявка» в win-app (RequestsWidget)
- CRUD подразделений (branches) через API — только GET список
- OpenAPI-спецификация (заглушка)
- Поддержка произвольных `doc_templates` в web-интерфейсе (templateKey хардкодирован)
- CRUD пользователей и ролей через admin API
