#include "app.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include <argon2.h>

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/exception/exception.hpp>
#include <bsoncxx/oid.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/options/index.hpp>
#include <mongocxx/uri.hpp>

namespace kisa {
namespace b = bsoncxx::builder::basic;
using b::kvp;

// ---------- JSON ----------
QHttpServerResponse jsonResp(const QJsonObject &o, QHttpServerResponse::StatusCode code) {
  return QHttpServerResponse("application/json", QJsonDocument(o).toJson(), code);
}

QHttpServerResponse errResp(const QString &code, const QString &msg,
                            QHttpServerResponse::StatusCode http) {
  return jsonResp({{"error", QJsonObject{{"code", code}, {"message", msg}}}}, http);
}

QJsonObject reqJson(const QHttpServerRequest &req, bool &ok) {
  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(req.body(), &err);
  ok = (err.error == QJsonParseError::NoError) && doc.isObject();
  return ok ? doc.object() : QJsonObject{};
}

// ---------- BSON ----------
// Через bsoncxx::to_json (Extended JSON) + QJsonDocument: не зависит от
// имен enum/struct конкретной версии драйвера.
// {"_id":{"$oid":"hex"}} схлопываем в {"_id":"hex"} для клиентов.
#include <bsoncxx/json.hpp>

QJsonObject docToJson(const bsoncxx::document::view &d) {
  const QByteArray raw = QByteArray::fromStdString(bsoncxx::to_json(d));
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject())
    return {};
  QJsonObject o = doc.object();
  if (o.contains("_id") && o.value("_id").isObject()) {
    const QJsonObject id = o.value("_id").toObject();
    if (id.contains("$oid"))
      o["_id"] = id.value("$oid").toString();
  }
  return o;
}

// b_date хранит миллисекунды с эпохи — сравниваем через time_point.
static bool bdateExpired(const bsoncxx::types::b_date &d) {
  return Clock::time_point(d.value) < Clock::now();
}

// ---------- crypto ----------
std::string randHex(std::size_t bytes) {
  QFile f(QStringLiteral("/dev/urandom"));
  QByteArray raw;
  if (f.open(QIODevice::ReadOnly))
    raw = f.read((qint64)bytes);
  if ((std::size_t)raw.size() != bytes) {
    // запасной путь (в контейнере практически недостижим)
    raw.resize((int)bytes);
    for (int i = 0; i < (int)bytes; ++i)
      raw[i] = (char)(QRandomGenerator::global()->generate() & 0xFF);
  }
  return raw.toHex().toStdString();
}

QString sha256Hex(const QByteArray &data) {
  return QString::fromLatin1(
      QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString argon2Hash(const QString &password) {
  // соль обязательна: 16 случайных байт
  QFile rnd(QStringLiteral("/dev/urandom"));
  QByteArray salt;
  if (rnd.open(QIODevice::ReadOnly))
    salt = rnd.read(16);
  if (salt.size() != 16)
    return {};
  const QByteArray pw = password.toUtf8();
  char out[128];
  // argon2id, t=3, m=64MB, p=1, hash 32 байта
  if (argon2id_hash_encoded(3, 1 << 16, 1, pw.constData(), (size_t)pw.size(),
                            salt.constData(), (size_t)salt.size(), 32, out,
                            sizeof(out)) != ARGON2_OK)
    return {};
  return QString::fromLatin1(out);
}

bool argon2Verify(const std::string &encoded, const QString &password) {
  const QByteArray pw = password.toUtf8();
  return argon2id_verify(encoded.c_str(), pw.constData(), (size_t)pw.size()) == ARGON2_OK;
}

// ---------- App ----------
App::App(const std::string &mongoUri) : client(mongocxx::uri{mongoUri}), db(client["kisa"]) {}

void App::ensureIndexes() {
  using mongocxx::options::index;
  auto tenants = db["tenants"];
  auto users = db["users"];
  auto sessions = db["sessions"];
  auto docs = db["doc_templates"];
  auto flows = db["workflow_templates"];

  tenants.create_index(b::make_document(kvp("slug", 1)),
                       index{}.unique(true));
  users.create_index(b::make_document(kvp("tenantId", 1), kvp("login", 1)),
                     index{}.unique(true));
  sessions.create_index(b::make_document(kvp("accessHash", 1)), index{}.unique(true));
  sessions.create_index(b::make_document(kvp("refreshHash", 1)), index{}.unique(true));
  index ttl;
  ttl.expire_after(std::chrono::seconds(0));
  sessions.create_index(b::make_document(kvp("refreshExp", 1)), ttl);
  docs.create_index(
      b::make_document(kvp("tenantId", 1), kvp("key", 1), kvp("version", 1)),
      index{}.unique(true));
  flows.create_index(
      b::make_document(kvp("tenantId", 1), kvp("key", 1), kvp("version", 1)),
      index{}.unique(true));
}

void App::seed() {
  auto tenants = db["tenants"];
  if (tenants.count_documents({}) > 0)
    return; // сидируем только пустую базу
  const QByteArray slug =
      qgetenv("SEED_TENANT_SLUG").isEmpty() ? QByteArray("demo") : qgetenv("SEED_TENANT_SLUG");
  const QByteArray login =
      qgetenv("SEED_ADMIN_LOGIN").isEmpty() ? QByteArray("admin") : qgetenv("SEED_ADMIN_LOGIN");
  const QByteArray pass = qgetenv("SEED_ADMIN_PASSWORD");
  if (pass.isEmpty()) {
    qWarning("seed skipped: set SEED_ADMIN_PASSWORD to create first tenant+admin");
    return;
  }
  const auto now = Clock::now();
  auto t = tenants.insert_one(
      b::make_document(kvp("slug", std::string(slug.constData())), kvp("name", std::string(slug.constData())),
                       kvp("status", "active"), kvp("createdAt", bsoncxx::types::b_date(now))));
  const bsoncxx::oid tid = t->inserted_id().get_oid().value;

  auto roles = db["roles"];
  roles.insert_one(b::make_document(
      kvp("tenantId", tid), kvp("roleKey", "tenant_admin"), kvp("title", "Администратор"),
      kvp("canAdminUsers", true), kvp("canAdminTemplates", true)));

  const QString phc = argon2Hash(QString::fromUtf8(pass));
  if (phc.isEmpty()) {
    qWarning("seed failed: argon2 error");
    return;
  }
  auto users = db["users"];
  users.insert_one(b::make_document(
      kvp("tenantId", tid), kvp("login", std::string(login.constData())),
      kvp("passwordHash", std::string(phc.toLatin1().constData())),
      kvp("name", "Administrator"), kvp("roles", b::make_array("tenant_admin")),
      kvp("status", "active"), kvp("createdAt", bsoncxx::types::b_date(now))));
  qInfo("seeded tenant '%s' with admin '%s'", slug.constData(), login.constData());
}

// ---------- tenant / principal ----------
std::string tenantIdFor(App &app, const QHttpServerRequest &req) {
  const QByteArray slug = req.value("X-Tenant"); // Qt 6.4 API
  if (slug.isEmpty())
    return {};
  try {
    auto doc = app.db["tenants"].find_one(
        b::make_document(kvp("slug", std::string(slug.constData()))));
    if (!doc)
      return {};
    return doc->view()["_id"].get_oid().value.to_string();
  } catch (...) {
    return {};
  }
}

std::optional<Principal> principalFor(App &app, const QHttpServerRequest &req) {
  const QByteArray auth = req.value("Authorization");
  if (!auth.startsWith("Bearer "))
    return std::nullopt;
  const QString aHash = sha256Hex(auth.mid(7));
  try {
    auto s = app.db["sessions"].find_one(
        b::make_document(kvp("accessHash", std::string(aHash.toLatin1().constData()))));
    if (!s)
      return std::nullopt;
    const auto sv = s->view();
    const auto accessExp = sv["accessExp"].get_date().value;
    if (Clock::time_point(accessExp) < Clock::now())
      return std::nullopt;
    const std::string tid = sv["tenantId"].get_oid().value.to_string();
    const std::string uid = sv["userId"].get_oid().value.to_string();
    // сверка tenant сессии с X-Tenant: tenantId никогда не берется из тела
    if (tid != tenantIdFor(app, req))
      return std::nullopt;
    auto u = app.db["users"].find_one(
        b::make_document(kvp("_id", bsoncxx::oid{uid})));
    if (!u || std::string(u->view()["status"].get_string().value) != "active")
      return std::nullopt;
    Principal p;
    p.tenantId = tid;
    p.userId = uid;
    const auto uv = u->view();
    p.login = std::string(uv["login"].get_string().value);
    p.name = uv["name"] ? std::string(uv["name"].get_string().value) : p.login;
    for (const auto &r : uv["roles"].get_array().value)
      p.roles.emplace_back(std::string(r.get_string().value));
    return p;
  } catch (...) {
    return std::nullopt;
  }
}

bool hasRole(const Principal &p, const char *role) {
  for (const auto &r : p.roles)
    if (r == role)
      return true;
  return false;
}

} // namespace kisa
