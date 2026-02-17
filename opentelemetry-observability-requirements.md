# OpenTelemetry Observability Stack — Requirements

> **Status**: Draft v2
> **Date**: 2026-02-17
> **Scope**: Observability-стек на базе OpenTelemetry для гибридной платформы (OpenShift + Linux VM, JVM-сервисы, Oracle DB)

---

## 1. Контекст и текущее состояние

### 1.1 Инфраструктура

| Компонент | Описание |
|---|---|
| **Linux VM** | Несколько десятков виртуальных машин Linux |
| **OpenShift** | Несколько десятков namespaces с Java/Kotlin приложениями |
| **Базы данных** | Oracle DB |
| **Языки/рантаймы** | Java, Kotlin (JVM-стек) |
| **Межсервисные транспорты** | REST (HTTP), gRPC, message brokers, возможно GraphQL |

### 1.2 Существующие Prometheus-экспортёры

Уже используются или требуют интеграции:

| Экспортёр | Среда | Назначение |
|---|---|---|
| **node_exporter** | Linux VM | Метрики ОС: CPU, RAM, disk, network, filesystem |
| **ssl_exporter** | Linux VM | Мониторинг сроков TLS-сертификатов, валидности цепочек |
| **process_exporter** | Linux VM | Метрики отдельных процессов: CPU, RSS, open FDs, threads |
| **oracledb_exporter** | Oracle DB | Метрики БД: sessions, tablespaces, wait events, query perf |
| **blackbox_exporter** | Сетевой уровень | Probe-мониторинг: HTTP, TCP, ICMP, DNS — доступность и latency эндпоинтов |

### 1.3 Существующий мониторинг

| Компонент | Текущее состояние | Стратегия |
|---|---|---|
| **Prometheus + Grafana** | Используется для метрик (экспортёры выше) | Миграция на VictoriaMetrics как long-term storage |
| **Splunk Enterprise** | Используется для логов | Сосуществование, постепенный переход на OpenSearch |
| Distributed tracing | Отсутствует | Новое внедрение через OTel |
| Continuous profiling | Отсутствует | Фаза 2+ |

---

## 2. Сигналы телеметрии

### 2.1 Metrics

**Приоритет**: Высокий

#### 2.1.1 Инфраструктурные метрики (VM)

- [ ] Сбор метрик из существующих экспортёров через OTel Collector (`prometheus` receiver)
  - `node_exporter` — scrape с каждой VM
  - `ssl_exporter` — scrape централизованно или с каждой VM
  - `process_exporter` — scrape с каждой VM
- [ ] Экспорт в VictoriaMetrics через `prometheusremotewrite` exporter
- [ ] Сохранение совместимости с существующими Grafana-дашбордами и recording rules

#### 2.1.2 Метрики баз данных

- [ ] `oracledb_exporter` → OTel Collector (`prometheus` receiver) → VictoriaMetrics
- [ ] Мониторинг: sessions, tablespace usage, wait events, top SQL, connection pool
- [ ] Алерты: tablespace > 80%, blocked sessions, long-running queries

#### 2.1.3 Probe-мониторинг

- [ ] `blackbox_exporter` → OTel Collector → VictoriaMetrics
- [ ] HTTP-пробы для ключевых эндпоинтов (health checks, API endpoints)
- [ ] TCP-пробы для инфраструктурных сервисов (Oracle listener, Kafka brokers)
- [ ] SSL certificate expiry alerting (дополнительно к ssl_exporter)

#### 2.1.4 Метрики приложений (OpenShift)

- [ ] JVM runtime-метрики через OTel Java Agent: heap, GC, threads, class loading
- [ ] Бизнес-метрики через OTel Metrics API (counters, histograms, gauges)
- [ ] RED-метрики (Rate, Errors, Duration) — автоматически из span-метрик (spanmetrics connector)
- [ ] OpenShift-специфичные метрики: kube-state-metrics, kubelet metrics

#### 2.1.5 Ключевые вопросы

- [ ] Какие recording/alerting rules уже определены в текущем Prometheus? Нужна миграция?
- [ ] Нужен ли federation или vmagent для сбора метрик из OpenShift built-in monitoring?
- [ ] Service discovery для экспортёров на VM — DNS-based, file-based, или Consul?

### 2.2 Distributed Traces

**Приоритет**: Высокий

- **Требования**:
  - [ ] Автоматическая инструментация JVM-сервисов в OpenShift (OTel Java Agent)
  - [ ] Пропагация контекста через все транспорты: HTTP, gRPC, Kafka/message broker headers
  - [ ] Корреляция trace ↔ logs через `trace_id`/`span_id` (MDC injection в Log4j2/Logback)
  - [ ] Поддержка sampling-стратегий: head-based и tail-based sampling
  - [ ] Визуализация trace waterfall, service map, dependency graph

- **Ключевые вопросы**:
  - [ ] Формат пропагации контекста: W3C TraceContext (рекомендуется) или B3 (если есть legacy)?
  - [ ] Tail-based sampling на фазе 1 или достаточно head-based?
  - [ ] Целевой sampling rate для production (рекомендация: 1–10% head-based + 100% для ошибок)

### 2.3 Logs

**Приоритет**: Средний

- **Требования**:
  - [ ] Инъекция `trace_id`, `span_id` в логи JVM-приложений (MDC)
  - [ ] Сбор логов из OpenShift через OTel Collector (filelog receiver / k8s container logs)
  - [ ] Сбор логов с VM через OTel Collector (filelog receiver)
  - [ ] Структурированное логирование (JSON) для новых сервисов
  - [ ] Экспорт в OpenSearch для хранения и полнотекстового поиска
  - [ ] Корреляция logs ↔ traces в Grafana (через OpenSearch data source + trace-to-logs)

- **Ключевые вопросы**:
  - [ ] Стратегия миграции с Splunk на OpenSearch — big bang или постепенно по сервисам?
  - [ ] Нужен ли dual-write (Splunk + OpenSearch) в переходный период?
  - [ ] Унификация формата логов: VM (syslog/файлы) vs. OpenShift (stdout/stderr)

### 2.4 Continuous Profiling

**Приоритет**: Низкий (фаза 2+, но архитектуру заложить сейчас)

- **Требования**:
  - [ ] CPU и allocation profiling для JVM-сервисов
  - [ ] Корреляция профилей с traces (span → profile)
  - [ ] Минимальный overhead в production (< 1% CPU)

- **Ключевые вопросы**:
  - [ ] Pyroscope (Grafana) vs. async-profiler + custom?
  - [ ] На каком этапе внедрять — после стабилизации traces + metrics?

---

## 3. Архитектура сбора данных (OTel Collector)

### 3.1 Deployment-паттерны

| Среда | Паттерн | Описание |
|---|---|---|
| **OpenShift** | DaemonSet (agent) + Deployment (gateway) | Agent на каждом узле, gateway для агрегации и маршрутизации |
| **Linux VM** | Standalone agent (systemd) | OTel Collector как systemd-сервис на каждой машине |

### 3.2 Централизованное управление конфигурацией (OpAMP)

**Ключевое требование**: Конфигурация всех Collector-инстансов управляется централизованно через протокол **OpAMP** (Open Agent Management Protocol).

- [ ] **OpAMP Server**: центральный компонент для управления конфигурациями
  - Хранение и версионирование конфигураций Collector
  - Push обновлений конфигурации на агенты без перезапуска
  - Мониторинг статуса всех агентов (health, version, effective config)
- [ ] **OpAMP Supervisor** на каждом Collector:
  - Приём обновлений конфигурации от OpAMP Server
  - Локальное применение конфигурации с graceful reload
  - Отчёт о статусе и effective config обратно на сервер
- [ ] **Группировка агентов**:
  - По среде: `openshift-agent`, `openshift-gateway`, `vm-agent`
  - По функции: `metrics-pipeline`, `traces-pipeline`, `logs-pipeline`
  - По окружению: `prod`, `staging`, `dev`
- [ ] **Rollout-стратегия**: canary-обновления конфигурации (сначала 1 агент → группа → все)

#### Ключевые вопросы (OpAMP)

- [ ] Какой OpAMP-сервер использовать? (BindPlane OP — наиболее зрелый, OpAMP-go reference, custom)
- [ ] Нужен ли UI для управления конфигурациями или достаточно API + GitOps?
- [ ] Как обеспечить fallback при недоступности OpAMP Server? (локальная конфигурация как fallback)
- [ ] Интеграция OpAMP с существующей системой CI/CD (конфигурации как код в git)?

### 3.3 Пайплайн Collector

```
Receivers → Processors → Connectors → Exporters

═══════════════════════════════════════════════════════
VM Agent pipeline:
═══════════════════════════════════════════════════════

Receivers:
  - prometheus         ← scrape node_exporter, ssl_exporter,
                         process_exporter, oracledb_exporter
  - filelog            ← логи приложений и системные логи
  - hostmetrics        ← дополнительные метрики хоста

Processors:
  - memory_limiter     ← защита от OOM
  - batch              ← группировка для эффективной отправки
  - attributes         ← обогащение: environment, host, datacenter
  - filter             ← фильтрация ненужных метрик
  - transform          ← PII masking в логах

Exporters:
  - prometheusremotewrite → VictoriaMetrics (metrics)
  - otlphttp              → OpenSearch (logs)

═══════════════════════════════════════════════════════
OpenShift Agent pipeline (DaemonSet):
═══════════════════════════════════════════════════════

Receivers:
  - otlp (gRPC + HTTP) ← от Java Agent в подах
  - filelog            ← container stdout/stderr logs
  - k8s_events         ← Kubernetes events

Processors:
  - memory_limiter
  - k8sattributes      ← обогащение pod/namespace/deployment metadata
  - batch
  - transform          ← PII masking

Exporters:
  - otlp → Gateway

═══════════════════════════════════════════════════════
OpenShift Gateway pipeline (Deployment):
═══════════════════════════════════════════════════════

Receivers:
  - otlp (gRPC + HTTP) ← от agents
  - prometheus         ← scrape blackbox_exporter, kube-state-metrics

Connectors:
  - spanmetrics        ← генерация RED-метрик из spans

Processors:
  - memory_limiter
  - batch
  - tail_sampling      ← sampling по error status, latency, service
  - attributes         ← нормализация атрибутов
  - transform          ← PII masking

Exporters:
  - prometheusremotewrite → VictoriaMetrics (metrics + spanmetrics)
  - otlphttp              → Tempo или Jaeger (traces)
  - otlphttp              → OpenSearch (logs)
```

### 3.4 Требования к Collector

- [ ] Единый бинарник Collector для OpenShift и VM (один дистрибутив, разные конфигурации через OpAMP)
- [ ] Отказоустойчивость gateway: минимум 2 реплики, load balancing
- [ ] Backpressure и retry при недоступности бэкендов
- [ ] Persistent queue для буферизации при кратковременных сбоях бэкендов
- [ ] Resource limits: CPU и memory ограничения (особенно на DaemonSet)
- [ ] Self-monitoring: метрики самого Collector (queue size, dropped data, export errors) → VictoriaMetrics

### 3.5 Ключевые вопросы

- [ ] Дистрибутив: `otelcol-contrib` (всё включено) или custom build (минимальный набор компонентов)?
- [ ] Нужен ли OpenTelemetry Operator для OpenShift (auto-injection Java Agent)?
- [ ] Как обеспечить service discovery для экспортёров на VM (file_sd, DNS-SD, Consul)?

---

## 4. Бэкенды хранения (Self-Hosted, OSS)

### 4.1 Целевой стек

| Сигнал | Бэкенд | Обоснование | Визуализация |
|---|---|---|---|
| **Metrics** | **VictoriaMetrics** | Высокая производительность, совместимость с PromQL, long-term storage, compression | Grafana |
| **Traces** | **Tempo** или **Jaeger** | OSS, интеграция с Grafana; Tempo — object storage native, Jaeger — mature | Grafana |
| **Logs** | **OpenSearch** | Полнотекстовый поиск, аналитика, Splunk-замена; зрелая экосистема | OpenSearch Dashboards + Grafana |
| **Profiles** | **Pyroscope** (фаза 2) | Grafana-интеграция, поддержка JVM, корреляция с traces | Grafana |

### 4.2 VictoriaMetrics — архитектура

- [ ] **Режим деплоя**: single-node или cluster (зависит от объёма)
  - Single-node: до ~1M active time series
  - Cluster (vminsert + vmselect + vmstorage): свыше 1M series, HA, горизонтальное масштабирование
- [ ] Приём данных: Prometheus remote_write от OTel Collector
- [ ] Совместимость: PromQL-запросы из Grafana, существующие дашборды работают без изменений
- [ ] vmalert для rule evaluation (recording rules + alerting rules) → Alertmanager
- [ ] vmagent как опциональный компонент для scrape (если нужен pull с VM без OTel Collector)

#### Ключевые вопросы (VictoriaMetrics)

- [ ] Ожидаемый объём active time series? (определяет single-node vs. cluster)
- [ ] Нужен ли vmagent отдельно или OTel Collector полностью заменяет его для scrape?
- [ ] Deduplication при HA-конфигурации (два Collector пишут одни метрики)?

### 4.3 Tempo / Jaeger — выбор trace-бэкенда

| Критерий | Tempo | Jaeger |
|---|---|---|
| Storage | Object storage (S3/MinIO) — дёшево для больших объёмов | Elasticsearch/Cassandra/ClickHouse — сложнее, но гибче |
| Поиск | По trace ID; Tag-based search через Tempo + search backend | Полноценный поиск по tags из коробки |
| Grafana-интеграция | Нативная (TraceQL) | Через data source plugin |
| Зрелость | Относительно молодой | Зрелый, CNCF graduated |
| Operational complexity | Низкая (stateless + object storage) | Средняя (зависит от storage backend) |

#### Ключевые вопросы (Traces)

- [ ] Tempo vs. Jaeger — какой предпочтительнее? (Tempo проще в эксплуатации, Jaeger богаче по поиску)
- [ ] Есть ли объектное хранилище (S3-compatible, MinIO) для Tempo?
- [ ] Если Jaeger — какой storage backend? (ClickHouse рекомендуется для self-hosted)

### 4.4 OpenSearch — логи

- [ ] Приём логов через OTLP (OTel Collector → OpenSearch exporter или data prepper)
- [ ] Index lifecycle management: hot → warm → cold → delete
- [ ] Index-per-service или index-per-day стратегия (зависит от объёма)
- [ ] Интеграция с Grafana через OpenSearch data source (для trace ↔ log correlation)
- [ ] OpenSearch Dashboards для аналитики и ad-hoc поиска

#### Ключевые вопросы (OpenSearch)

- [ ] Есть ли уже OpenSearch-кластер или нужно поднимать с нуля?
- [ ] Ожидаемый объём логов в день (GB/день)?
- [ ] Нужен ли Data Prepper (OpenSearch ingestion pipeline) или прямой экспорт из OTel Collector?

### 4.5 Retention-политика

| Сигнал | Hot (быстрый доступ) | Warm/Cold | Примечание |
|---|---|---|---|
| **Metrics** | 30 дней (full resolution) | 90–365 дней (downsampled) | VictoriaMetrics: встроенный downsampling, `-retentionPeriod` |
| **Traces** | 7–14 дней | 30 дней (object storage) | Для Tempo — автоматическая ротация в object storage |
| **Logs** | 14–30 дней | 60–90 дней (warm/cold tier) | OpenSearch ISM policy для автоматической ротации |
| **Profiles** | 7 дней | 14 дней | Профили объёмные, оперативная ценность коротка |

- [ ] Retention должна быть **управляемой**: настраиваемая через конфигурацию без изменения кода/инфраструктуры
- [ ] Retention-политики разные по окружениям (prod — дольше, staging/dev — короче)
- [ ] Мониторинг объёма хранимых данных и алерты на превышение квот

---

## 5. Security

### 5.1 TLS

- [ ] Шифрование трафика между всеми компонентами:
  - OTel Java Agent → Collector (OTLP gRPC/HTTP с TLS)
  - Collector Agent → Collector Gateway (mTLS)
  - Collector → VictoriaMetrics, Tempo/Jaeger, OpenSearch (TLS)
  - OpAMP Supervisor → OpAMP Server (TLS)
- [ ] Автоматическая ротация сертификатов (cert-manager в OpenShift, Vault или PKI для VM)

### 5.2 RBAC

- [ ] **Grafana RBAC**: разграничение доступа к дашбордам и данным по командам/ролям
  - Developers: доступ к данным своих сервисов
  - SRE: полный доступ ко всем данным
  - Support: read-only доступ к дашбордам и алертам
  - Management: доступ к SLO/SLI-дашбордам
- [ ] **OpenSearch Security**: role-based доступ к индексам логов (по namespace/команде)
- [ ] **VictoriaMetrics**: multi-tenancy через vmauth или label-based access control
- [ ] **OTel Collector Gateway**: аутентификация входящих соединений (mTLS или bearer token)

### 5.3 Обнаружение утечек PII

- [ ] **PII detection pipeline** в OTel Collector:
  - Regex-based обнаружение: email, телефоны, ИНН, паспорт, номера карт
  - Маскирование/удаление обнаруженных PII в span attributes и log body
  - Реализация через `transform` processor с regexp-правилами
- [ ] **Мониторинг PII-утечек**:
  - Метрика количества замаскированных PII-полей (по типу, по сервису)
  - Алерт при аномальном всплеске PII-обнаружений (потенциальная регрессия в коде)
  - Периодический аудит: sample-проверка данных в бэкендах на наличие пропущенных PII
- [ ] **Конфигурируемые правила**: список паттернов PII управляется централизованно (через OpAMP или ConfigMap)
- [ ] **Allowlist/denylist** для span attributes и log fields

### 5.4 Ключевые вопросы

- [ ] Есть ли PKI / cert-manager / Vault для управления сертификатами?
- [ ] Конкретный перечень PII-паттернов для маскирования (зависит от домена)
- [ ] Нужен ли audit log для доступа к observability-данным?
- [ ] Интеграция аутентификации с существующим IdP (LDAP, OIDC)?

---

## 6. Alerting

### 6.1 Архитектура алертинга

```
VictoriaMetrics (vmalert)
        │
        ├── Recording rules (aggregation, precomputation)
        ├── Alerting rules (thresholds, SLO-based)
        │
        ▼
  Alertmanager
        │
        ├── Deduplication, grouping, silencing
        ├── Routing по severity и команде
        │
        ▼
  Notification channels (Slack, PagerDuty, OpsGenie, email)
```

### 6.2 SLO-Based Alerting

- [ ] Определение SLI для каждого сервиса:
  - **Availability**: % успешных запросов (HTTP 2xx/3xx)
  - **Latency**: % запросов быстрее target (p99 < X ms)
  - **Throughput**: RPS в пределах ожидаемого диапазона
- [ ] Burn-rate alerting (Google SRE model):
  - Fast burn: 2% error budget за 1 час → page
  - Slow burn: 5% error budget за 6 часов → ticket
  - Пример: `1 - (rate(http_requests_total{status=~"5.."}[1h]) / rate(http_requests_total[1h])) < SLO`
- [ ] SLO-дашборды: error budget remaining, burn rate, SLI trends

### 6.3 Инфраструктурные алерты

- [ ] **VM**: disk usage > 85%, CPU sustained > 90%, memory < 10% free, process down
- [ ] **Oracle DB**: tablespace > 80%, blocked sessions > N, long queries > X min
- [ ] **SSL**: certificate expiry < 30 / 14 / 7 дней (ssl_exporter + blackbox_exporter)
- [ ] **Blackbox**: endpoint down > 2 min, latency > threshold
- [ ] **OTel Collector**: queue overflow, export errors, dropped data

### 6.4 Ключевые вопросы

- [ ] Какие notification channels нужны? (Slack, PagerDuty, OpsGenie, email, MS Teams?)
- [ ] Есть ли определённые SLO/SLI для существующих сервисов или нужно определить с нуля?
- [ ] Нужна ли on-call ротация через Alertmanager или используется внешняя система?
- [ ] Разделение алертов по severity: critical (page) / warning (ticket) / info (dashboard)?

---

## 7. Пользовательский опыт (Developer Experience)

### 7.1 Для разработчиков

- [ ] **Zero-code start**: OTel Java Agent автоматически инструментирует HTTP, gRPC, JDBC, Kafka, JMS
- [ ] **Custom instrumentation**: SDK-библиотеки для кастомных спанов и метрик (internal starter/BOM)
- [ ] **Onboarding**: документация «как подключить tracing к своему сервису» (< 30 мин)
- [ ] **Local dev**: возможность видеть трейсы локально (Jaeger all-in-one / OTLP → console exporter)
- [ ] **Grafana-дашборды**: шаблон «Service Overview» для каждого сервиса:
  - RED-метрики (rate, errors, duration)
  - JVM runtime (heap, GC, threads)
  - Downstream dependencies
  - Recent traces с ошибками

### 7.2 Для поддержки (L2/L3 support)

- [ ] **Incident investigation flow**: алерт → дашборд → трейсы → логи (единый UI в Grafana)
- [ ] **Trace search**: поиск трейсов по correlation ID, user ID, request parameters
- [ ] **Log search**: полнотекстовый поиск в OpenSearch Dashboards
- [ ] **Runbooks**: ссылки из алертов на инструкции по диагностике

### 7.3 Для SRE

- [ ] **Service map / dependency graph** на основе trace data
- [ ] **Cross-service trace exploration** с фильтрацией по атрибутам
- [ ] **Корреляция**: trace → logs → metrics в одном UI (Grafana Explore)
- [ ] **SLO dashboard**: burn rate, error budget, SLI trends по всей платформе
- [ ] **Capacity planning**: метрики объёма телеметрии, storage usage, прогноз роста
- [ ] **Collector fleet management**: OpAMP UI — статус агентов, версии, конфигурации
- [ ] **Toil reduction**: automated runbook suggestions, anomaly detection (фаза 2+)

### 7.4 Ключевые вопросы

- [ ] Нужен ли shared internal library (Spring Boot starter) для стандартизации инструментации?
- [ ] Как интегрировать OTel Java Agent в CI/CD и OpenShift deployment pipeline?
- [ ] Требуется ли обучение команд? В каком формате (workshop, docs, видео)?
- [ ] Есть ли correlation ID / request ID, который проходит через все сервисы?

---

## 8. Фазы внедрения (предварительно)

> Точный план реализации будет составлен отдельно на основании утверждённых требований.

### Фаза 0 — Инфраструктура и пилот (~ 3–4 недели)

- VictoriaMetrics: deployment, импорт существующих recording/alerting rules
- OTel Collector: deployment на 2–3 VM (systemd) и в 1–2 OpenShift namespaces
- OpAMP Server: базовая настройка, подключение пилотных Collector
- Миграция scrape экспортёров (node, ssl, process, oracledb, blackbox) с Prometheus на OTel Collector
- Grafana: подключение VictoriaMetrics data source, проверка существующих дашбордов

### Фаза 1 — Трассировка + логи (~ 4–6 недель)

- Tempo или Jaeger: deployment
- Auto-instrumentation 3–5 пилотных сервисов в OpenShift (OTel Java Agent)
- Context propagation через HTTP, gRPC, message broker
- OpenSearch: deployment, OTel Collector log pipeline
- Trace ↔ log correlation (trace_id в MDC)
- Базовые дашборды: Service Overview, trace explorer

### Фаза 2 — Масштабирование (~ 4–8 недель)

- Rollout OTel Agent на все сервисы в OpenShift
- Rollout OTel Collector на все VM
- OpAMP: полное управление конфигурациями fleet'а
- Tail-based sampling на gateway
- PII detection/masking pipeline
- SLO-based alerting (vmalert + Alertmanager)
- Миграция логов со Splunk → OpenSearch (постепенно по сервисам)

### Фаза 3 — Зрелость (ongoing)

- Continuous profiling (Pyroscope)
- Service map и dependency analysis
- Advanced anomaly detection
- Capacity optimization и cost monitoring
- Обучение и документация

---

## 9. Нефункциональные требования

| Требование | Значение | Примечание |
|---|---|---|
| **Overhead на приложение** | < 3% CPU, < 50 MB RAM | OTel Java Agent overhead |
| **Latency добавленная** | < 1 ms на span export | Async export, batch processing |
| **Доступность Collector gateway** | 99.9% | HA: минимум 2 реплики |
| **Доступность бэкендов** | 99.9% | VictoriaMetrics, Tempo/Jaeger, OpenSearch |
| **Потеря данных** | < 0.1% при нормальной работе | Retry + persistent queue в Collector |
| **Время от деплоя до видимости** | < 2 минут | Метрики/трейсы доступны в Grafana |
| **Масштабируемость** | До 100+ сервисов | Горизонтальное масштабирование gateway и бэкендов |
| **Config rollout (OpAMP)** | < 5 минут на весь fleet | С canary strategy |
| **PII masking coverage** | 100% spans/logs проходят через transform | Без bypass |

---

## 10. Открытые вопросы и решения

> Эти вопросы должны быть закрыты до начала реализации.

| # | Вопрос | Варианты | Решение |
|---|---|---|---|
| Q1 | Trace-бэкенд | Tempo (проще) / Jaeger (зрелее) | **TBD** |
| Q2 | OpAMP Server | BindPlane OP / custom / OpAMP-go reference impl | **TBD** |
| Q3 | VictoriaMetrics mode | Single-node / Cluster | **TBD** |
| Q4 | OpenSearch — новый или существующий | Новый кластер / есть существующий | **TBD** |
| Q5 | Формат пропагации контекста | W3C TraceContext / B3 | **TBD** |
| Q6 | Service discovery для VM экспортёров | file_sd / DNS-SD / Consul | **TBD** |
| Q7 | OTel Operator для OpenShift | Да (auto-inject) / Нет (ручной) | **TBD** |
| Q8 | Дистрибутив Collector | otelcol-contrib / custom build | **TBD** |
| Q9 | Объектное хранилище для Tempo | S3 / MinIO / нет (Jaeger + ClickHouse) | **TBD** |
| Q10 | Notification channels для алертов | Slack / PagerDuty / OpsGenie / email / Teams | **TBD** |
| Q11 | IdP для RBAC | LDAP / OIDC / другое | **TBD** |
| Q12 | Миграция Splunk → OpenSearch | Постепенная / big bang / dual-write | **TBD** |

---

## Приложение A: Глоссарий

| Термин | Описание |
|---|---|
| **OTel** | OpenTelemetry — CNCF-проект, открытый стандарт для сбора телеметрии |
| **Collector** | OTel Collector — агент/gateway для приёма, обработки и экспорта телеметрии |
| **OpAMP** | Open Agent Management Protocol — протокол для централизованного управления агентами (Collector) |
| **Span** | Единица работы в распределённом трейсе (вызов метода, HTTP-запрос, SQL-запрос) |
| **RED** | Rate, Errors, Duration — ключевые метрики для микросервисов |
| **MDC** | Mapped Diagnostic Context — механизм Log4j2/Logback для добавления контекста в логи |
| **SLO/SLI** | Service Level Objective / Indicator — целевые показатели качества сервиса |
| **Burn rate** | Скорость расходования error budget — основа SLO-based alerting |
| **Tail-based sampling** | Решение о сохранении трейса после завершения всех спанов (по error, latency) |
| **VictoriaMetrics** | OSS TSDB, совместимая с Prometheus, оптимизированная для high cardinality и long-term storage |
| **OpenSearch** | OSS fork Elasticsearch для логов и полнотекстового поиска |
| **Pyroscope** | Continuous profiling платформа (Grafana), поддержка JVM |
